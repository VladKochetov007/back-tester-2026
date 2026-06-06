#include "sim/SimulatedLOB.hpp"

namespace cmf::sim {

std::size_t SimulatedLOB::snapshot(Side side, std::vector<Level> &out,
                                   std::size_t maxN) const {
  out.clear();
  if (maxN == 0)
    return 0;

  const auto own = view_->own_levels(side); // ascending by price
  const std::size_t own_n = own.size();
  const auto own_at = [&](std::size_t j) -> Level {
    const auto &l = (side == Side::Buy) ? own[own_n - 1 - j] : own[j];
    return Level{l.first, l.second};
  };

  std::vector<Level> hist;
  const std::size_t hist_cap = maxN + own_n;
  hist.reserve(hist_cap);
  basement_->for_each_level_until(
      side, [&](ScaledPrice p, LimitOrderBook::AggQty q) {
        const Qty net = view_->net_basement_qty(side, p, static_cast<Qty>(q));
        if (net > 0)
          hist.emplace_back(p, net);
        return hist.size() < hist_cap;
      });

  std::size_t i = 0, j = 0;
  while (out.size() < maxN && (i < hist.size() || j < own_n)) {
    if (j >= own_n) {
      out.push_back(hist[i++]);
    } else if (i >= hist.size()) {
      out.push_back(own_at(j++));
    } else if (const Level o = own_at(j); hist[i].first == o.first) {
      out.emplace_back(hist[i].first, hist[i].second + o.second);
      ++i;
      ++j;
    } else if (price_is_better(side, hist[i].first, o.first)) {
      out.push_back(hist[i++]);
    } else {
      out.push_back(own_at(j++));
    }
  }
  return out.size();
}

} // namespace cmf::sim
