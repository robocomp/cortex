#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <threadpool/threadpool.h>
#include <variant>

namespace DSR {

struct Node;
struct Edge;
struct SignalInfo;

enum SignalMode { QT, Queue };

typedef std::function<void(std::uint64_t, const std::string &, SignalInfo)>
    update_node_signal_t;
typedef std::function<void(std::uint64_t, const std::vector<std::string> &,
                           SignalInfo)>
    update_node_attr_signal_t;
typedef std::function<void(std::uint64_t, std::uint64_t, const std::string &,
                           SignalInfo)>
    update_edge_signal_t;
typedef std::function<void(std::uint64_t, std::uint64_t, const std::string &,
                           const std::vector<std::string> &, SignalInfo)>
    update_edge_attr_signal_t;
typedef std::function<void(std::uint64_t, std::uint64_t, const std::string &,
                           SignalInfo)>
    del_edge_signal_t;
typedef std::function<void(std::uint64_t, SignalInfo)> del_node_signal_t;
typedef std::function<void(const Node &, SignalInfo)> deleted_node_signal_t;
typedef std::function<void(const Edge &, SignalInfo)> deleted_edge_signal_t;

typedef std::function<void(std::uint64_t, const std::string &)>
    update_node_signal_noinfo_t;
typedef std::function<void(std::uint64_t, const std::vector<std::string> &)>
    update_node_attr_signal_noinfo_t;
typedef std::function<void(std::uint64_t, std::uint64_t, const std::string &)>
    update_edge_signal_noinfo_t;
typedef std::function<void(std::uint64_t, std::uint64_t, const std::string &,
                           const std::vector<std::string> &)>
    update_edge_attr_signal_noinfo_t;
typedef update_edge_signal_noinfo_t del_edge_signal_noinfo_t;
typedef std::function<void(std::uint64_t)> del_node_signal_noinfo_t;
typedef std::function<void(const Node &)> deleted_node_signal_noinfo_t;
typedef std::function<void(const Edge &)> deleted_edge_signal_noinfo_t;

typedef std::variant<update_node_signal_noinfo_t, update_node_attr_signal_noinfo_t,
                     update_edge_signal_noinfo_t, update_edge_attr_signal_noinfo_t,
                     del_node_signal_noinfo_t,
                     deleted_node_signal_noinfo_t, deleted_edge_signal_noinfo_t>
    signal_fn_ptr_t;

struct QueuedSignalRunner {
  ThreadPool tp;
  uint8_t log_level{1}; // GraphSettings::LOGLEVEL as uint8_t (0=DEBUG, 1=INFO, 2=WARNING, 3=ERROR)
  std::vector<update_node_signal_noinfo_t> uns_fns;
  std::vector<update_node_attr_signal_noinfo_t> unas_fns;
  std::vector<update_edge_signal_noinfo_t> ues_fns;
  std::vector<update_edge_attr_signal_noinfo_t> ueas_fns;
  std::vector<del_edge_signal_noinfo_t> des_fns;
  std::vector<del_node_signal_noinfo_t> den_fns;
  std::vector<deleted_node_signal_noinfo_t> dn_fns;
  std::vector<deleted_edge_signal_noinfo_t> de_fns;
  explicit QueuedSignalRunner() : tp(2)
  {}

  void connect(signal_fn_ptr_t fn, const std::string& type) {
    std::visit(
        [&, this](auto &&arg) {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, update_node_attr_signal_noinfo_t>)
            unas_fns.push_back(arg);
          else if constexpr (std::is_same_v<T, update_node_signal_noinfo_t>)
            uns_fns.push_back(arg);
          else if constexpr (std::is_same_v<T, update_edge_signal_noinfo_t>) {
            if (type == "UPDATE_EDGE")
              ues_fns.push_back(arg);
            else
              des_fns.push_back(arg);
          }
          else if constexpr (std::is_same_v<T, update_edge_attr_signal_noinfo_t>)
            ueas_fns.push_back(arg);
          else if constexpr (std::is_same_v<T, del_node_signal_noinfo_t>)
            den_fns.push_back(arg);
          else if constexpr (std::is_same_v<T, deleted_node_signal_noinfo_t>)
            dn_fns.push_back(arg);
          else if constexpr (std::is_same_v<T, deleted_edge_signal_noinfo_t>)
            de_fns.push_back(arg);
          else
            std::cerr << "Python signals don't use SignalInfo parameter\n";
        },
        fn);
  }

  void run_update_node_signal(std::uint64_t a, const std::string &b,
                              SignalInfo c);
  void run_update_node_attr_signal(std::uint64_t a,
                                   const std::vector<std::string> &b,
                                   SignalInfo c);
  void run_update_edge_signal(std::uint64_t a, std::uint64_t b,
                              const std::string &c, SignalInfo d);
  void run_update_edge_attr_signal(std::uint64_t a, std::uint64_t b,
                                   const std::string &c,
                                   const std::vector<std::string> &d,
                                   SignalInfo e);
  void run_del_edge_signal(std::uint64_t a, std::uint64_t b,
                           const std::string &c, SignalInfo d);
  void run_del_node_signal(std::uint64_t a, SignalInfo b);
  void run_deleted_node_signal(const Node &a, SignalInfo b);
  void run_deleted_edge_signal(const Edge &a, SignalInfo b);
};

struct signals_fns {
  update_node_signal_t update_node_signal;
  update_node_attr_signal_t update_node_attr_signal;
  update_edge_signal_t update_edge_signal;
  update_edge_attr_signal_t update_edge_attr_signal;
  del_edge_signal_t del_edge_signal;
  del_node_signal_t del_node_signal;
  deleted_node_signal_t deleted_node_signal;
  deleted_edge_signal_t deleted_edge_signal;
  std::unique_ptr<QueuedSignalRunner> runner;
};
}
; // namespace DSR