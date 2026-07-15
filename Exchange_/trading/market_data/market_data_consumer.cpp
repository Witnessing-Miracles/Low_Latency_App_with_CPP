#include "market_data_consumer.h"

namespace Trading {
    MarketDataConsumer::MarketDataConsumer(Common::ClientId client_id, Exchange::MEMarketUpdateLFQueue *market_updates,
                                            const std::string &iface,
                                            const std::string &snapshot_ip, int snapshot_port,
                                            const std::string &incremental_ip, int incremental_port)
        : incoming_md_updates_(market_updates), run_(false), logger_("trading_market_data_consumer_" + std::to_string(client_id) +
            ".log"), incremental_mcast_socket_(logger_), snapshot_mcast_socket_(logger_), iface_(iface),
            snapshot_ip_(snapshot_ip), snapshot_port_(snapshot_port) {
        auto recv_callback = [this](auto socket) {
            recvCallback(socket);
        };

        incremental_mcast_socket_.recv_callback_ = recv_callback;
        ASSERT(incremental_mcast_socket_.init(incremental_ip, iface, incremental_port, true) >= 0,
                "Unable to create incremental mcast socket. error:" + std::string(std::strerror(errno)));

        ASSERT(incremental_mcast_socket_.join(incremental_ip),
                "Join failed on:" + std::to_string(incremental_mcast_socket_.socket_fd_) + " error:" + std::string(std::strerror(errno)));

        snapshot_mcast_socket_.recv_callback_ = recv_callback;
    }

    /// Main loop for this thread - reads and processes messages from the multicast sockets - the heavy lifting is in the recvCallback()
    /// and checkSnapshotSync() methods.
    auto MarketDataConsumer::run() noexcept -> void {
        logger_.log("%:% %() %\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_));
        while (run_) {
            incremental_mcast_socket_.sendAndRecv();
            snapshot_mcast_socket_.sendAndRecv();
        }
    }

    /// Start the process of snapshot synchronization by subscribing to the snapshot multicast stream.
    auto MarketDataConsumer::startSnapshotSync() -> void {
        snapshot_queued_msgs_.clear();
        incremental_queued_msgs_.clear();

        ASSERT(snapshot_mcast_socket_.init(snapshot_ip_, iface_, snapshot_port_, /* is_listening */ true) >= 0,
                "Unable to create snapshot mcast socket. error:" + std::string(std::strerror(errno)));
        ASSERT(snapshot_mcast_socket_.join(snapshot_ip_), // IGMP multicast subscription.
                "Join failed on:" + std::to_string(snapshot_mcast_socket_.socket_fd_) + "error:" + std::string(std::strerror(errno)));
    }

    /// Queue up a message in the *_queued_msgs_ container, first parameter specifies if this update came from the snapshot or the incremental streams.
    auto MarketDataConsumer::queueMessage(bool is_snapshot, const Exchange::MDPMarketUpdate *request) {
        if (is_snapshot) {
            if (snapshot_queued_msgs_.find(request->seq_num_) != snapshot_queued_msgs_.end()) {
                logger_.log("%:% %() % Packet drops on snapshot socket. Received for a 2nd time:%\n", __FILE__, __LINE__, __FUNCTION__,
                            Common::getCurrentTimeStr(&time_str_), request->toString());
                snapshot_queued_msgs_.clear();
            }
            snapshot_queued_msgs_[request->seq_num_] = request->me_market_update_;
        } else {
            incremental_queued_msgs_[request->seq_num_] = request->me_market_update_;
        }

        logger_.log("%:% %() % size snapshot:% incremental:% % => %\n", __FILE__, __LINE__, __FUNCTION__,
                    Common::getCurrentTimeStr(&time_str_), snapshot_queued_msgs_.size(), incremental_queued_msgs_.size(), request->toString());

        checkSnapshotSync();
    }

    /// Process a market data update, the consumer needs to use the socket parameter to figure out whether this came from snapshot/incremental stream
    auto MarketDataConsumer::recvCallback(McastSocket *socket) noexcept -> void {
        const auto is_snapshot = (socket->socket_fd_ == snapshot_mcast_socket_.socket_fd_);
        if (UNLIKELY(is_snapshot && !in_recovery_)) {   // market update was read from the snapshot market data stream and we are not in recovery,
            // so we dont need it and discard it.
            socket->next_rcv_valid_index_ = 0;

            logger_.log("%:% %() % WARN not expect snapshot messages.\n",
                        __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_));
            return;
        }

        if (socket->next_rcv_valid_index_ >= sizeof(Exchange::MDPMarketUpdate)) {
            size_t i = 0;
            for (; i + sizeof(Exchange::MDPMarketUpdate) <= socket->next_rcv_valid_index_; i += sizeof(Exchange::MDPMarketUpdate)) {
                auto request = reinterpret_cast<const Exchange::MDPMarketUpdate *>(socket->inbound_data_.data() + i);
                logger_.log("%:% %() % Received % socket len:% %\n", __FILE__, __LINE__, __FUNCTION__,
                            Common::getCurrentTimeStr(&time_str_),
                            (is_snapshot ? "snapshot" : "incremental"), sizeof(Exchange::MDPMarketUpdate), request->toString());

                const bool already_in_recovery = in_recovery_;
                in_recovery_ = (already_in_recovery || request->seq_num_ != next_exp_inc_seq_num_);

                if (UNLIKELY(in_recovery_)) {
                    if (UNLIKELY(!already_in_recovery)) {
                        // if we just entered recovery, start the snapshot synchonization process by subscribing to the snapshot multicast stream.
                        logger_.log("%:% %() % Packet drops on % socket. SeqNum expected:% received:%\n", __FILE__, __LINE__, __FUNCTION__,
                            Common::getCurrentTimeStr(&time_str_), (is_snapshot ? "snapshot" : "incremental"), next_exp_inc_seq_num_, request->seq_num_);

                        startSnapshotSync();
                    }

                    queueMessage(is_snapshot, request); // queue up the market data update message and check if snapshot recovery / synchronization
                    // can be completed successfully.
                } else if (!is_snapshot) {
                    // not in recovery and received a packet in the correct order and without gaps, process it.
                    logger_.log("%:% %() % %\n", __FILE__, __LINE__, __FUNCTION__,
                                Common::getCurrentTimeStr(&time_str_), request->toString());
                    
                    ++next_exp_inc_seq_num_;

                    auto next_write = incoming_md_updates_->getNextToWriteTo();
                    *next_write = std::move(request->me_market_update_);
                    incoming_md_updates_->updateWriteIndex();
                }
            }
            memcpy(socket->inbound_data_.data(), socket->inbound_data_.data() + i, socket->next_rcv_valid_index_ - i);
            socket->next_rcv_valid_index_ -= i;
        }
    }
}