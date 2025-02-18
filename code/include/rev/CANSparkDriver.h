/*
 * Copyright (c) 2018-2024 REV Robotics
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of REV Robotics nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <stdint.h>

typedef enum {
    c_Spark_kForward = 0,
    c_Spark_kReverse = 1,
} c_Spark_LimitDirection;

typedef enum {
    c_Spark_kNormallyOpen = 0,
    c_Spark_kNormallyClosed = 1,
} c_Spark_LimitPolarity;

typedef enum {
    c_Spark_kNoSensor = 0,
    c_Spark_kPrimaryEncoder = 1,
    c_Spark_kAnalogSensor = 2,
    c_Spark_kAlternateOrExternalEncoder = 3,
    c_Spark_kDutyCycleSensor = 4
} c_Spark_ClosedLoopControlSensor;

typedef enum {
    c_Spark_SparkMax,
    c_Spark_SparkFlex,
    c_Spark_Unknown = 255
} c_Spark_SparkModel;

typedef enum { c_Spark_kCoast = 0, c_Spark_kBrake = 1 } c_Spark_IdleMode;

typedef enum { c_Spark_kPWM = 0, c_Spark_kCAN = 1 } c_Spark_InputMode;

typedef enum {
    c_Spark_Fault_kOther = 0,
    c_Spark_Fault_kMotorType = 1,
    c_Spark_Fault_kSensor = 2,
    c_Spark_Fault_kCan = 3,
    c_Spark_Fault_kTemperature = 4,
    c_Spark_Fault_kDrv = 5,
    c_Spark_Fault_kEscEeprom = 6,
    c_Spark_Fault_kFirmware = 7
} c_Spark_Fault;

typedef enum {
    c_Spark_Warning_kBrownout = 0,
    c_Spark_Warning_kOvercurrent = 1,
    c_Spark_Warning_kEscEeprom = 2,
    c_Spark_Warning_kExtEeprom = 3,
    c_Spark_Warning_kSensor = 4,
    c_Spark_Warning_kStall = 5,
    c_Spark_Warning_kHasReset = 6,
    c_Spark_Warning_kOther = 7
} c_Spark_Warning;

typedef enum { c_Spark_kBrushed = 0, c_Spark_kBrushless = 1 } c_Spark_MotorType;

typedef enum {
    c_Spark_kStatus0 = 0,
    c_Spark_kStatus1 = 1,
    c_Spark_kStatus2 = 2,
    c_Spark_kStatus3 = 3,
    c_Spark_kStatus4 = 4,
    c_Spark_kStatus5 = 5,
    c_Spark_kStatus6 = 6,
    c_Spark_kStatus7 = 7,
} c_Spark_PeriodicFrame;

typedef enum {
    c_Spark_kDutyCycle = 0,
    c_Spark_kVelocity = 1,
    c_Spark_kVoltage = 2,
    c_Spark_kPosition = 3,
    c_Spark_kSmartMotion = 4,
    c_Spark_kCurrent = 5,
    c_Spark_kSmartVelocity = 6,
    c_Spark_kMAXMotionPositionControl = 7,
    c_Spark_kMAXMotionVelocityControl = 8
} c_Spark_ControlType;

typedef enum {
    c_Spark_kDataPortConfigInvalid =
        -1,  // For internal use only, cannot be sent to the SPARK
    c_Spark_kDataPortConfigDefault = 0,
    c_Spark_kDataPortConfigAltEncoder = 1,
} c_Spark_DataPortConfig;

typedef struct {
    float appliedOutput;
    float voltage;
    float current;
    uint8_t motorTemperature;
    uint8_t hardForwardLimitReached;
    uint8_t hardReverseLimitReached;
    uint8_t softForwardLimitReached;
    uint8_t softReverseLimitReached;
    uint8_t inverted;
    uint8_t primaryHeartbeatLock;
    uint64_t timestamp;
} c_Spark_PeriodicStatus0;

typedef struct {
    uint8_t otherFault;
    uint8_t motorTypeFault;
    uint8_t sensorFault;
    uint8_t canFault;
    uint8_t temperatureFault;
    uint8_t drvFault;
    uint8_t escEepromFault;
    uint8_t firmwareFault;
    uint8_t brownoutWarning;
    uint8_t overcurrentWarning;
    uint8_t escEepromWarning;
    uint8_t extEepromWarning;
    uint8_t sensorWarning;
    uint8_t stallWarning;
    uint8_t hasResetWarning;
    uint8_t otherWarning;
    uint8_t otherStickyFault;
    uint8_t motorTypeStickyFault;
    uint8_t sensorStickyFault;
    uint8_t canStickyFault;
    uint8_t temperatureStickyFault;
    uint8_t drvStickyFault;
    uint8_t escEepromStickyFault;
    uint8_t firmwareStickyFault;
    uint8_t brownoutStickyWarning;
    uint8_t overcurrentStickyWarning;
    uint8_t escEepromStickyWarning;
    uint8_t extEepromStickyWarning;
    uint8_t sensorStickyWarning;
    uint8_t stallStickyWarning;
    uint8_t hasResetStickyWarning;
    uint8_t otherStickyWarning;
    uint8_t isFollower;
    uint64_t timestamp;
} c_Spark_PeriodicStatus1;

typedef struct {
    float primaryEncoderVelocity;
    float primaryEncoderPosition;
    uint64_t timestamp;
} c_Spark_PeriodicStatus2;

typedef struct {
    float analogVoltage;
    float analogVelocity;
    float analogPosition;
    uint64_t timestamp;
} c_Spark_PeriodicStatus3;

typedef struct {
    float externalOrAltEncoderVelocity;
    float externalOrAltEncoderPosition;
    uint64_t timestamp;
} c_Spark_PeriodicStatus4;

typedef struct {
    float dutyCycleEncoderVelocity;
    float dutyCycleEncoderPosition;
    uint64_t timestamp;
} c_Spark_PeriodicStatus5;

typedef struct {
    float unadjustedDutyCycle;
    float dutyCyclePeriod;
    uint8_t dutyCycleNoSignal;
    uint64_t timestamp;
} c_Spark_PeriodicStatus6;

typedef struct {
    float iAccumulation;
    uint64_t timestamp;
} c_Spark_PeriodicStatus7;

typedef struct {
    uint8_t major;
    uint8_t minor;
    uint16_t build;
    uint8_t debugBuild;
    uint32_t versionRaw;
} c_Spark_FirmwareVersion;

typedef enum {
    c_Spark_kStrategyTrapezoidal = 0,
    c_Spark_kStrategySCurve = 1
} c_Spark_AccelStrategy;

typedef enum {
    c_Spark_kMAXMotionTrapezoidal = 0,
    // c_Spark_kMAXMotionSCurve = 1
} c_Spark_MAXMotionPositionMode;

typedef enum {
    c_Spark_kAbsolute = 0,
    c_Spark_kRelative = 1
} c_Spark_AnalogMode;

typedef enum {
    c_Spark_kDutyCycleAbsolute = 0,
    c_Spark_kDutyCycleRelative = 1
} c_Spark_DutyCycleMode;
