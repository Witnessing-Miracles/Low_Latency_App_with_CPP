#!/bin/bash

# ./trading_main CLIENT_ID ALGO_TYPE [CLIP_1 THRESH_1 MAX_ORDER_SIZE_1 MAX_POS_1 MAX_LOSS_1] [CLIP_2 THRESH_2 MAX_ORDER_SIZE_2 MAX_POS_2 MAX_LOSS_2] ...

./cmake-build-release/trading_main  1 MAKER \
                                  100 0.6 150 300 -100 \
                                  60 0.6 150 300 -100 \
                                  150 0.5 250 600 -100 \
                                  200 0.4 500 3000 -100 \
                                  1000 0.9 5000 4000 -100 \
                                  300 0.8 1500 3000 -100 \
                                  50 0.7 150 300 -100 \
                                  100 0.3 250 300 -100 &
sleep 5

wait