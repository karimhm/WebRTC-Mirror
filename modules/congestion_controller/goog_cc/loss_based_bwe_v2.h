/*
 *  Copyright 2021 The WebRTC project authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_CONGESTION_CONTROLLER_GOOG_CC_LOSS_BASED_BWE_V2_H_
#define MODULES_CONGESTION_CONTROLLER_GOOG_CC_LOSS_BASED_BWE_V2_H_

#include <deque>
#include <vector>

#include "absl/types/optional.h"
#include "api/array_view.h"
#include "api/field_trials_view.h"
#include "api/network_state_predictor.h"
#include "api/transport/network_types.h"
#include "api/units/data_rate.h"
#include "api/units/data_size.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"

namespace webrtc {

// State of the loss based estimate, which can be either increasing/decreasing
// when network is loss limited, or equal to the delay based estimate.
enum class LossBasedState {
  kIncreasing = 0,
  kDecreasing = 1,
  kDelayBasedEstimate = 2
};

class LossBasedBweV2 {
 public:
  struct Result {
    ~Result() = default;
    DataRate bandwidth_estimate = DataRate::Zero();
    // State is used by goog_cc, which later sends probe requests to probe
    // controller if state is kIncreasing.
    LossBasedState state = LossBasedState::kDelayBasedEstimate;
  };
  // Creates a disabled `LossBasedBweV2` if the
  // `key_value_config` is not valid.
  explicit LossBasedBweV2(const FieldTrialsView* key_value_config);

  LossBasedBweV2(const LossBasedBweV2&) = delete;
  LossBasedBweV2& operator=(const LossBasedBweV2&) = delete;

  ~LossBasedBweV2() = default;

  bool IsEnabled() const;
  // Returns true iff a BWE can be calculated, i.e., the estimator has been
  // initialized with a BWE and then has received enough `PacketResult`s.
  bool IsReady() const;

  // Returns `DataRate::PlusInfinity` if no BWE can be calculated.
  Result GetLossBasedResult() const;

  void SetAcknowledgedBitrate(DataRate acknowledged_bitrate);
  void SetBandwidthEstimate(DataRate bandwidth_estimate);
  void SetMinMaxBitrate(DataRate min_bitrate, DataRate max_bitrate);
  void UpdateBandwidthEstimate(
      rtc::ArrayView<const PacketResult> packet_results,
      DataRate delay_based_estimate,
      BandwidthUsage delay_detector_state,
      absl::optional<DataRate> probe_bitrate,
      DataRate upper_link_capacity,
      bool in_alr);

 private:
  struct ChannelParameters {
    double inherent_loss = 0.0;
    DataRate loss_limited_bandwidth = DataRate::MinusInfinity();
  };

  struct Config {
    double bandwidth_rampup_upper_bound_factor = 0.0;
    double rampup_acceleration_max_factor = 0.0;
    TimeDelta rampup_acceleration_maxout_time = TimeDelta::Zero();
    std::vector<double> candidate_factors;
    double higher_bandwidth_bias_factor = 0.0;
    double higher_log_bandwidth_bias_factor = 0.0;
    double inherent_loss_lower_bound = 0.0;
    double loss_threshold_of_high_bandwidth_preference = 0.0;
    double bandwidth_preference_smoothing_factor = 0.0;
    DataRate inherent_loss_upper_bound_bandwidth_balance =
        DataRate::MinusInfinity();
    double inherent_loss_upper_bound_offset = 0.0;
    double initial_inherent_loss_estimate = 0.0;
    int newton_iterations = 0;
    double newton_step_size = 0.0;
    bool append_acknowledged_rate_candidate = true;
    bool append_delay_based_estimate_candidate = false;
    TimeDelta observation_duration_lower_bound = TimeDelta::Zero();
    int observation_window_size = 0;
    double sending_rate_smoothing_factor = 0.0;
    double instant_upper_bound_temporal_weight_factor = 0.0;
    DataRate instant_upper_bound_bandwidth_balance = DataRate::MinusInfinity();
    double instant_upper_bound_loss_offset = 0.0;
    double temporal_weight_factor = 0.0;
    double bandwidth_backoff_lower_bound_factor = 0.0;
    bool trendline_integration_enabled = false;
    int trendline_observations_window_size = 0;
    double max_increase_factor = 0.0;
    TimeDelta delayed_increase_window = TimeDelta::Zero();
    bool use_acked_bitrate_only_when_overusing = false;
    bool not_increase_if_inherent_loss_less_than_average_loss = false;
    double high_loss_rate_threshold = 1.0;
    DataRate bandwidth_cap_at_high_loss_rate = DataRate::MinusInfinity();
    double slope_of_bwe_high_loss_func = 1000.0;
    bool probe_integration_enabled = false;
    TimeDelta probe_expiration = TimeDelta::Zero();
    bool bound_by_upper_link_capacity_when_loss_limited = false;
    bool not_use_acked_rate_in_alr = false;
  };

  struct Derivatives {
    double first = 0.0;
    double second = 0.0;
  };

  struct Observation {
    bool IsInitialized() const { return id != -1; }

    int num_packets = 0;
    int num_lost_packets = 0;
    int num_received_packets = 0;
    DataRate sending_rate = DataRate::MinusInfinity();
    int id = -1;
  };

  struct PartialObservation {
    int num_packets = 0;
    int num_lost_packets = 0;
    DataSize size = DataSize::Zero();
  };

  static absl::optional<Config> CreateConfig(
      const FieldTrialsView* key_value_config);
  bool IsConfigValid() const;

  // Returns `0.0` if not enough loss statistics have been received.
  double GetAverageReportedLossRatio() const;
  std::vector<ChannelParameters> GetCandidates(bool in_alr) const;
  DataRate GetCandidateBandwidthUpperBound() const;
  Derivatives GetDerivatives(const ChannelParameters& channel_parameters) const;
  double GetFeasibleInherentLoss(
      const ChannelParameters& channel_parameters) const;
  double GetInherentLossUpperBound(DataRate bandwidth) const;
  double AdjustBiasFactor(double loss_rate, double bias_factor) const;
  double GetHighBandwidthBias(DataRate bandwidth) const;
  double GetObjective(const ChannelParameters& channel_parameters) const;
  DataRate GetSendingRate(DataRate instantaneous_sending_rate) const;
  DataRate GetInstantUpperBound() const;
  void CalculateInstantUpperBound();

  void CalculateTemporalWeights();
  void NewtonsMethodUpdate(ChannelParameters& channel_parameters) const;

  // Returns false if there exists a kBwOverusing or kBwUnderusing in the
  // window.
  bool TrendlineEsimateAllowBitrateIncrease() const;

  // Returns true if there exists an overusing state in the window.
  bool TrendlineEsimateAllowEmergencyBackoff() const;

  // Returns false if no observation was created.
  bool PushBackObservation(rtc::ArrayView<const PacketResult> packet_results,
                           BandwidthUsage delay_detector_state);
  void UpdateTrendlineEstimator(
      const std::vector<PacketResult>& packet_feedbacks,
      Timestamp at_time);
  void UpdateDelayDetector(BandwidthUsage delay_detector_state);
  bool IsEstimateIncreasingWhenLossLimited(
      const ChannelParameters& best_candidate);
  bool IsBandwidthLimitedDueToLoss() const;
  void SetProbeBitrate(absl::optional<DataRate> probe_bitrate);
  bool IsRequestingProbe() const;

  absl::optional<DataRate> acknowledged_bitrate_;
  absl::optional<Config> config_;
  ChannelParameters current_estimate_;
  int num_observations_ = 0;
  std::vector<Observation> observations_;
  PartialObservation partial_observation_;
  Timestamp last_send_time_most_recent_observation_ = Timestamp::PlusInfinity();
  Timestamp last_time_estimate_reduced_ = Timestamp::MinusInfinity();
  absl::optional<DataRate> cached_instant_upper_bound_;
  std::vector<double> instant_upper_bound_temporal_weights_;
  std::vector<double> temporal_weights_;
  std::deque<BandwidthUsage> delay_detector_states_;
  Timestamp recovering_after_loss_timestamp_ = Timestamp::MinusInfinity();
  DataRate bandwidth_limit_in_current_window_ = DataRate::PlusInfinity();
  DataRate min_bitrate_ = DataRate::KilobitsPerSec(1);
  DataRate max_bitrate_ = DataRate::PlusInfinity();
  LossBasedState current_state_ = LossBasedState::kDelayBasedEstimate;
  DataRate probe_bitrate_ = DataRate::PlusInfinity();
  DataRate delay_based_estimate_ = DataRate::PlusInfinity();
  DataRate upper_link_capacity_ = DataRate::PlusInfinity();
  Timestamp last_probe_timestamp_ = Timestamp::MinusInfinity();
};

}  // namespace webrtc

#endif  // MODULES_CONGESTION_CONTROLLER_GOOG_CC_LOSS_BASED_BWE_V2_H_
