#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <vector>

#include "prometheus/metric.h"
#include "prometheus/family.h"

#include "prometheus/detail/ckms_quantiles.h"
#include "prometheus/detail/time_window_quantiles.h"

namespace prometheus {

  /// \brief A summary metric samples observations over a sliding window of time.
  ///
  /// This class represents the metric type summary:
  /// https://prometheus.io/docs/instrumenting/writing_clientlibs/#summary
  ///
  /// A summary provides a total count of observations and a sum of all observed
  /// values. In contrast to a histogram metric it also calculates configurable
  /// Phi-quantiles over a sliding window of time.
  ///
  /// The essential difference between summaries and histograms is that summaries
  /// calculate streaming Phi-quantiles on the client side and expose them
  /// directly, while histograms expose bucketed observation counts and the
  /// calculation of quantiles from the buckets of a histogram happens on the
  /// server side:
  /// https://prometheus.io/docs/prometheus/latest/querying/functions/#histogram_quantile.
  ///
  /// Note that Phi designates the probability density function of the standard
  /// Gaussian distribution.
  ///
  /// See https://prometheus.io/docs/practices/histograms/ for detailed
  /// explanations of Phi-quantiles, summary usage, and differences to histograms.
  ///
  /// The class is thread-safe. No concurrent call to any API of this type causes
  /// a data race.
  class Summary : Metric {

  public:

    using Value  = double;
    using Family = CustomFamily<Summary>;

    static const Metric::Type static_type = Metric::Type::Summary;

    using Quantiles = std::vector<detail::CKMSQuantiles::Quantile>;

    const Quantiles             quantiles_;
    mutable std::mutex          mutex_;
    std::uint64_t               count_;
    double                      sum_;
    detail::TimeWindowQuantiles quantile_values_;

  public:

    /// \brief Create a summary metric.
    ///
    /// \param quantiles A list of 'targeted' Phi-quantiles. A targeted
    /// Phi-quantile is specified in the form of a Phi-quantile and tolerated
    /// error. For example a Quantile{0.5, 0.1} means that the median (= 50th
    /// percentile) should be returned with 10 percent error or a Quantile{0.2,
    /// 0.05} means the 20th percentile with 5 percent tolerated error. Note that
    /// percentiles and quantiles are the same concept, except percentiles are
    /// expressed as percentages. The Phi-quantile must be in the interval [0, 1].
    /// Note that a lower tolerated error for a Phi-quantile results in higher
    /// usage of resources (memory and cpu) to calculate the summary.
    ///
    /// The Phi-quantiles are calculated over a sliding window of time. The
    /// sliding window of time is configured by max_age and age_buckets.
    ///
    /// \param max_age Set the duration of the time window, i.e., how long
    /// observations are kept before they are discarded. The default value is 60
    /// seconds.
    ///
    /// \param age_buckets Set the number of buckets of the time window. It
    /// determines the number of buckets used to exclude observations that are
    /// older than max_age from the summary, e.g., if max_age is 60 seconds and
    /// age_buckets is 5, buckets will be switched every 12 seconds. The value is
    /// a trade-off between resources (memory and cpu for maintaining the bucket)
    /// and how smooth the time window is moved. With only one age bucket it
    /// effectively results in a complete reset of the summary each time max_age
    /// has passed. The default value is 5.
    Summary(const Quantiles& quantiles, std::chrono::milliseconds max_age = std::chrono::seconds{ 60 }, int age_buckets = 5)
      : Metric(static_type), quantiles_{ quantiles }, count_{ 0 }, sum_{ 0 }, quantile_values_(quantiles_, max_age, age_buckets) {}

    /// \brief Observe the given amount.
    void Observe(const double value) {
      std::lock_guard<std::mutex> lock(mutex_);

      count_ += 1;
      sum_ += value;
      quantile_values_.insert(value);

    }

    /// \brief Get the current value of the summary.
    ///
    /// Collect is called by the Registry when collecting metrics.
    virtual ClientMetric Collect() const {
      auto metric = ClientMetric{};

      std::lock_guard<std::mutex> lock(mutex_);

      metric.summary.quantile.reserve(quantiles_.size());
      for (const auto& quantile : quantiles_) {
        auto metricQuantile = ClientMetric::Quantile{};
        metricQuantile.quantile = quantile.quantile;
        metricQuantile.value = quantile_values_.get(quantile.quantile);
        metric.summary.quantile.push_back(std::move(metricQuantile));
      }
      metric.summary.sample_count = count_;
      metric.summary.sample_sum = sum_;

      return metric;
    }
  };

} // namespace prometheus
