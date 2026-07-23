#pragma once

#include "../../common/macros.h"
#include "../../common/logging.h"

#include "order_manager.h"
#include "feature_engine.h"

using namespace Common;

namespace Trading {
    class MarketMaker {
    public:
        MarketMaker(Common::Logger *logger, TradeEngine *trade_engine, const FeatureEngine *feature_engine,
                    OrderManager *order_manager,
                    const TradeEngineCfgHashMap &ticker_cfg);
    
    private:
        /// The feature engine that drives the market making algorithm.
        const FeatureEngine *feature_engine_ = nullptr;

        /// Used by the market making algorithm to manage its passive orders.
        OrderManager *order_manager_ = nullptr;

        std::string time_str_;
        Common::Logger *logger_ = nullptr;

        /// Holds the trading configuration for the market making algorithm.
        const TradeEngineCfgHashMap ticker_cfg_;
    };
}