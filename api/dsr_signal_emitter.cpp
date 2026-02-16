#include <dsr/api/dsr_api.h>
#include <dsr/api/dsr_signal_emitter.h>
#include <dsr/api/dsr_signal_info.h>
#include <dsr/api/dsr_logging.h>

void DSR::QueuedSignalRunner::run_update_node_signal(std::uint64_t a,
                                                     const std::string &b,
                                                     SignalInfo c) {
  DSR_LOG_DEBUG_L(static_cast<DSR::GraphSettings::LOGLEVEL>(log_level), "[SIGNAL] update_node id:", a, "type:", b);
  tp.spawn_task([=, this] {
    for (auto fn : uns_fns) {
      if (fn)
        fn(a, b);
    }
  });
}
void DSR::QueuedSignalRunner::run_update_node_attr_signal(
    std::uint64_t a, const std::vector<std::string> &b, SignalInfo c) {
  DSR_LOG_DEBUG_L(static_cast<DSR::GraphSettings::LOGLEVEL>(log_level), "[SIGNAL] update_node_attr id:", a);
  tp.spawn_task([=, this] {
    for (auto fn : unas_fns) {
      if (fn)
        fn(a, b);
    }
  });
}
void DSR::QueuedSignalRunner::run_update_edge_signal(std::uint64_t a,
                                                     std::uint64_t b,
                                                     const std::string &c,
                                                     SignalInfo d) {
  DSR_LOG_DEBUG_L(static_cast<DSR::GraphSettings::LOGLEVEL>(log_level), "[SIGNAL] update_edge from:", a, "to:", b, "type:", c);
  tp.spawn_task([=, this] {
    for (auto fn : ues_fns) {
      if (fn)
        fn(a, b, c);
    }
  });
}

void DSR::QueuedSignalRunner::run_update_edge_attr_signal(
    std::uint64_t a, std::uint64_t b, const std::string &c,
    const std::vector<std::string> &d, SignalInfo e) {
  DSR_LOG_DEBUG_L(static_cast<DSR::GraphSettings::LOGLEVEL>(log_level), "[SIGNAL] update_edge_attr from:", a, "to:", b, "type:", c);
  tp.spawn_task([=, this] {
    for (auto fn : ueas_fns) {
      if (fn)
        fn(a, b, c, d);
    }
  });
}

void DSR::QueuedSignalRunner::run_del_edge_signal(std::uint64_t a,
                                                  std::uint64_t b,
                                                  const std::string &c,
                                                  SignalInfo d) {
  DSR_LOG_DEBUG_L(static_cast<DSR::GraphSettings::LOGLEVEL>(log_level), "[SIGNAL] del_edge from:", a, "to:", b, "type:", c);
  tp.spawn_task([=, this] {
    for (auto fn : des_fns) {
      if (fn)
        fn(a, b, c);
    }
  });
}
void DSR::QueuedSignalRunner::run_del_node_signal(std::uint64_t a,
                                                  SignalInfo b) {
  DSR_LOG_DEBUG_L(static_cast<DSR::GraphSettings::LOGLEVEL>(log_level), "[SIGNAL] del_node id:", a);
  tp.spawn_task([=, this] {
    for (auto fn : den_fns) {
      if (fn)
        fn(a);
    }
  });
}

void DSR::QueuedSignalRunner::run_deleted_node_signal(const Node &a,
                                                      SignalInfo b) {
  DSR_LOG_DEBUG_L(static_cast<DSR::GraphSettings::LOGLEVEL>(log_level), "[SIGNAL] deleted_node name:", a.name(), "id:", a.id());
  tp.spawn_task([=, this] {
    for (auto fn : dn_fns) {
      if (fn)
        fn(a);
    }
  });
}
void DSR::QueuedSignalRunner::run_deleted_edge_signal(const Edge &a,
                                                      SignalInfo b) {
  DSR_LOG_DEBUG_L(static_cast<DSR::GraphSettings::LOGLEVEL>(log_level), "[SIGNAL] deleted_edge from:", a.from(), "to:", a.to(), "type:", a.type());
  tp.spawn_task([=, this] {
    for (auto fn : de_fns) {
      if (fn)
        fn(a);
    }
  });
}
