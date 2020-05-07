/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "neuralnetworks_hidl_hal_test"

#include "VtsHalNeuralnetworks.h"

namespace android::hardware::neuralnetworks::V1_3::vts::functional {

using implementation::PreparedModelCallback;
using V1_0::DeviceStatus;
using V1_0::PerformanceInfo;
using V1_1::ExecutionPreference;
using V1_2::Constant;
using V1_2::DeviceType;
using V1_2::Extension;
using HidlToken = hidl_array<uint8_t, static_cast<uint32_t>(Constant::BYTE_SIZE_OF_CACHE_TOKEN)>;

// create device test
TEST_P(NeuralnetworksHidlTest, CreateDevice) {}

// status test
TEST_P(NeuralnetworksHidlTest, StatusTest) {
    Return<DeviceStatus> status = kDevice->getStatus();
    ASSERT_TRUE(status.isOk());
    EXPECT_EQ(DeviceStatus::AVAILABLE, static_cast<DeviceStatus>(status));
}

// initialization
TEST_P(NeuralnetworksHidlTest, GetCapabilitiesTest) {
    using OperandPerformance = Capabilities::OperandPerformance;
    Return<void> ret = kDevice->getCapabilities_1_3([](ErrorStatus status,
                                                       const Capabilities& capabilities) {
        EXPECT_EQ(ErrorStatus::NONE, status);

        auto isPositive = [](const PerformanceInfo& perf) {
            return perf.execTime > 0.0f && perf.powerUsage > 0.0f;
        };

        EXPECT_TRUE(isPositive(capabilities.relaxedFloat32toFloat16PerformanceScalar));
        EXPECT_TRUE(isPositive(capabilities.relaxedFloat32toFloat16PerformanceTensor));
        const auto& opPerf = capabilities.operandPerformance;
        EXPECT_TRUE(std::all_of(
                opPerf.begin(), opPerf.end(),
                [isPositive](const OperandPerformance& a) { return isPositive(a.info); }));
        EXPECT_TRUE(std::is_sorted(opPerf.begin(), opPerf.end(),
                                   [](const OperandPerformance& a, const OperandPerformance& b) {
                                       return a.type < b.type;
                                   }));
        EXPECT_TRUE(std::all_of(opPerf.begin(), opPerf.end(), [](const OperandPerformance& a) {
            return a.type != OperandType::SUBGRAPH;
        }));
        EXPECT_TRUE(isPositive(capabilities.ifPerformance));
        EXPECT_TRUE(isPositive(capabilities.whilePerformance));
    });
    EXPECT_TRUE(ret.isOk());
}

// detect cycle
TEST_P(NeuralnetworksHidlTest, CycleTest) {
    // opnd0 = TENSOR_FLOAT32            // model input
    // opnd1 = TENSOR_FLOAT32            // model input
    // opnd2 = INT32                     // model input
    // opnd3 = ADD(opnd0, opnd4, opnd2)
    // opnd4 = ADD(opnd1, opnd3, opnd2)
    // opnd5 = ADD(opnd4, opnd0, opnd2)  // model output
    //
    //            +-----+
    //            |     |
    //            v     |
    // 3 = ADD(0, 4, 2) |
    // |                |
    // +----------+     |
    //            |     |
    //            v     |
    // 4 = ADD(1, 3, 2) |
    // |                |
    // +----------------+
    // |
    // |
    // +-------+
    //         |
    //         v
    // 5 = ADD(4, 0, 2)

    const std::vector<Operand> operands = {
            {
                    // operands[0]
                    .type = OperandType::TENSOR_FLOAT32,
                    .dimensions = {1},
                    .numberOfConsumers = 2,
                    .scale = 0.0f,
                    .zeroPoint = 0,
                    .lifetime = OperandLifeTime::SUBGRAPH_INPUT,
                    .location = {.poolIndex = 0, .offset = 0, .length = 0},
            },
            {
                    // operands[1]
                    .type = OperandType::TENSOR_FLOAT32,
                    .dimensions = {1},
                    .numberOfConsumers = 1,
                    .scale = 0.0f,
                    .zeroPoint = 0,
                    .lifetime = OperandLifeTime::SUBGRAPH_INPUT,
                    .location = {.poolIndex = 0, .offset = 0, .length = 0},
            },
            {
                    // operands[2]
                    .type = OperandType::INT32,
                    .dimensions = {},
                    .numberOfConsumers = 3,
                    .scale = 0.0f,
                    .zeroPoint = 0,
                    .lifetime = OperandLifeTime::SUBGRAPH_INPUT,
                    .location = {.poolIndex = 0, .offset = 0, .length = 0},
            },
            {
                    // operands[3]
                    .type = OperandType::TENSOR_FLOAT32,
                    .dimensions = {1},
                    .numberOfConsumers = 1,
                    .scale = 0.0f,
                    .zeroPoint = 0,
                    .lifetime = OperandLifeTime::TEMPORARY_VARIABLE,
                    .location = {.poolIndex = 0, .offset = 0, .length = 0},
            },
            {
                    // operands[4]
                    .type = OperandType::TENSOR_FLOAT32,
                    .dimensions = {1},
                    .numberOfConsumers = 2,
                    .scale = 0.0f,
                    .zeroPoint = 0,
                    .lifetime = OperandLifeTime::TEMPORARY_VARIABLE,
                    .location = {.poolIndex = 0, .offset = 0, .length = 0},
            },
            {
                    // operands[5]
                    .type = OperandType::TENSOR_FLOAT32,
                    .dimensions = {1},
                    .numberOfConsumers = 0,
                    .scale = 0.0f,
                    .zeroPoint = 0,
                    .lifetime = OperandLifeTime::SUBGRAPH_OUTPUT,
                    .location = {.poolIndex = 0, .offset = 0, .length = 0},
            },
    };

    const std::vector<Operation> operations = {
            {.type = OperationType::ADD, .inputs = {0, 4, 2}, .outputs = {3}},
            {.type = OperationType::ADD, .inputs = {1, 3, 2}, .outputs = {4}},
            {.type = OperationType::ADD, .inputs = {4, 0, 2}, .outputs = {5}},
    };

    Subgraph subgraph = {
            .operands = operands,
            .operations = operations,
            .inputIndexes = {0, 1, 2},
            .outputIndexes = {5},
    };
    const Model model = {
            .main = std::move(subgraph),
            .referenced = {},
            .operandValues = {},
            .pools = {},
    };

    // ensure that getSupportedOperations_1_2() checks model validity
    ErrorStatus supportedOpsErrorStatus = ErrorStatus::GENERAL_FAILURE;
    Return<void> supportedOpsReturn = kDevice->getSupportedOperations_1_3(
            model, [&model, &supportedOpsErrorStatus](ErrorStatus status,
                                                      const hidl_vec<bool>& supported) {
                supportedOpsErrorStatus = status;
                if (status == ErrorStatus::NONE) {
                    ASSERT_EQ(supported.size(), model.main.operations.size());
                }
            });
    ASSERT_TRUE(supportedOpsReturn.isOk());
    ASSERT_EQ(supportedOpsErrorStatus, ErrorStatus::INVALID_ARGUMENT);

    // ensure that prepareModel_1_3() checks model validity
    sp<PreparedModelCallback> preparedModelCallback = new PreparedModelCallback;
    Return<ErrorStatus> prepareLaunchReturn = kDevice->prepareModel_1_3(
            model, ExecutionPreference::FAST_SINGLE_ANSWER, Priority::MEDIUM, {},
            hidl_vec<hidl_handle>(), hidl_vec<hidl_handle>(), HidlToken(), preparedModelCallback);
    ASSERT_TRUE(prepareLaunchReturn.isOk());
    //     Note that preparation can fail for reasons other than an
    //     invalid model (invalid model should result in
    //     INVALID_ARGUMENT) -- for example, perhaps not all
    //     operations are supported, or perhaps the device hit some
    //     kind of capacity limit.
    EXPECT_NE(prepareLaunchReturn, ErrorStatus::NONE);
    EXPECT_NE(preparedModelCallback->getStatus(), ErrorStatus::NONE);
    EXPECT_EQ(preparedModelCallback->getPreparedModel(), nullptr);
}

}  // namespace android::hardware::neuralnetworks::V1_3::vts::functional
