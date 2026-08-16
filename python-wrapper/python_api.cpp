//
// Created by juancarlos on 18/11/20.
//

#include "dsr/api/dsr_api.h"
#include <dsr/api/dsr_signal_emitter.h>
#include <stdexcept>
#include <thread>

#include <QCoreApplication>
#include <QObject>
#include <QThread>

using namespace DSR;

#include "include/silent_output.h"
#include "include/signal_function_caster.h"
#include "include/custom_bind_map.h"
#include "include/custom_bool_cast.h"
#include "include/custom_vector_cast.h"
#include "include/GHistory.h"

#pragma push_macro("slots")
#undef slots

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/eigen.h>
#include <pybind11/stl_bind.h>
#include <pybind11/functional.h>
#include <pybind11/numpy.h>

#pragma pop_macro("slots")

#include <utility>

#include <memory>
#include <functional>



namespace py = pybind11;

using namespace py::literals;
//using namespace RoboCompDSRGetID;


enum ATT_ENUM: uint16_t {
    STRING_PY,
    BOOL_PY,
    NPYU8,
    NPYF,
    NPYU64,
    VECU8_PY,
    VECF_PY,
    VECU64_PY,
    VEC2_PY,
    VEC3_PY,
    VEC4_PY,
    VEC6_PY,
    U64_PY,
    DOUBLE_PY,
    FLOAT_PY,
    I32_PY,
    U32_PY
};

using attribute_type = std::variant<std::string,
                                    no_int_cast_bool,
                                    py::array_t<uint8_t>,
                                    py::array_t<float>,
                                    py::array_t<uint64_t>,
                                    std::vector<uint8_t>,
                                    std::vector<float>,
                                    std::vector<uint64_t>,
                                    std::array<float, 2>,
                                    std::array<float, 3>,
                                    std::array<float, 4>,
                                    std::array<float, 6>,
                                    uint64_t,
                                    double,
                                    float,
                                    int32_t,
                                    uint32_t>;



template<std::size_t idx, typename T>
Value convert_variant_fn(const attribute_type & e)
{
    //std::cout << "[PYTHON_VARIANT -> Value] " << attribute_type_TYPENAMES_UNION[e.index()] << std::endl;
    Value vout;
    if constexpr (std::is_same_v<T, py::array_t<uint8_t>>)
    {
        auto tmp = std::get<py::array_t<uint8_t>>(e);
        const auto size = tmp.size();
        py::buffer_info x = tmp.request();
        vout.emplace<idx>(std::vector<uint8_t>{static_cast<uint8_t *>(x.ptr), static_cast<uint8_t *>(x.ptr) + size});
    } else if constexpr (std::is_same_v<T, py::array_t<float>>)
    {
        auto tmp = std::get<py::array_t<float>>(e);
        //std::cout << tmp.dtype() << " " << tmp.size() << py::array_t<float>::ensure(tmp) << std::endl;
        const auto size = tmp.size();
        py::buffer_info x = tmp.request();
        vout.emplace<idx>(std::vector<float>{static_cast<float *>(x.ptr), static_cast<float *>(x.ptr) + size});
    } else if constexpr (std::is_same_v<T, py::array_t<uint64_t>>)
    {
        auto tmp = std::get<py::array_t<uint64_t>>(e);
        const auto size = tmp.size();
        py::buffer_info x = tmp.request();
        vout.emplace<idx>(std::vector<uint64_t>{static_cast<uint64_t *>(x.ptr), static_cast<uint64_t *>(x.ptr) + size});
    } else if constexpr (std::is_same_v<T, no_int_cast_bool>)
    {
        auto tmp = std::get<no_int_cast_bool>(e)();
        vout.emplace<idx>(tmp);
    }
    else
    {
        auto x = std::get<T>(e);
        vout.emplace<idx>(x);
    }

    return vout;
};



Value convert_variant(const attribute_type & e)
{
    typedef Value (*conver_fn) (const attribute_type &);
    constexpr std::array<conver_fn, 17> cast = { convert_variant_fn<0, std::string>,
                                                 convert_variant_fn<4, no_int_cast_bool>,
                                                 convert_variant_fn<5, py::array_t<uint8_t>>,
                                                 convert_variant_fn<3, py::array_t<float>>,
                                                 convert_variant_fn<9, py::array_t<uint64_t>>,
                                                 convert_variant_fn<5, std::vector<uint8_t>>,
                                                 convert_variant_fn<3, std::vector<float>>,
                                                 convert_variant_fn<9, std::vector<uint64_t>>,
                                                 convert_variant_fn<10, std::array<float, 2>>,
                                                 convert_variant_fn<11, std::array<float, 3>>,
                                                 convert_variant_fn<12, std::array<float, 4>>,
                                                 convert_variant_fn<13, std::array<float, 6>>,
                                                 convert_variant_fn<7, uint64_t>,
                                                 convert_variant_fn<8, double>,
                                                 convert_variant_fn<2, float>,
                                                 convert_variant_fn<1, int32_t>,
                                                 convert_variant_fn<6, uint32_t>
                                             };

    const auto idx = e.index(); //idx_Value.at(e.index());
    return cast[idx](e);
}

PYBIND11_MAKE_OPAQUE(std::map<std::pair<uint64_t, std::string>, Edge>)
PYBIND11_MAKE_OPAQUE(std::map<std::string, Attribute>)


// ── Qt event pump for remote graph applies ───────────────────────────────────────────────────────
// dsr_api.cpp marshals every REMOTE apply -- the initial full-graph import and the node/edge/attr
// delta batches -- with QMetaObject::invokeMethod(this, ..., Qt::QueuedConnection). A queued invoke
// only ever runs if the target object's thread is spinning a Qt event loop. A plain Python script
// has none, so without this the applies pile up and are NEVER executed: DDS discovery succeeds, the
// full-graph answer arrives, the constructor reports "Synchronized" -- and the graph then stays
// permanently empty (get_nodes() -> []), deltas included.
//
// Signals do not need this: pydsr builds the graph with SignalMode::Queue, whose QueuedSignalRunner
// dispatches callbacks on its own ThreadPool.
//
// So pydsr owns one dedicated QThread running an event loop, and the DSRGraph lives on it. The graph
// is both CONSTRUCTED and DESTROYED there, so its thread affinity is correct for its whole lifetime
// and no posted event is ever delivered to a thread that has no loop or is already gone. Reading the
// graph from Python stays safe: DSRGraph serialises every accessor with its own shared_mutex.
class GraphEventPump
{
public:
    static GraphEventPump &instance()
    {
        // Intentionally leaked: the pump must outlive every DSRGraph, including graphs still being
        // torn down at interpreter shutdown, so it must not be a destroyed-at-exit static.
        static GraphEventPump *pump = new GraphEventPump();
        return *pump;
    }

    // Run `fn` on the pump thread, blocking until it has finished.
    template <typename F>
    void run_blocking(F &&fn)
    {
        QMetaObject::invokeMethod(context_, std::forward<F>(fn), Qt::BlockingQueuedConnection);
    }

private:
    GraphEventPump()
    {
        ensure_qapp();
        thread_ = new QThread();
        thread_->setObjectName("pydsr-events");
        context_ = new QObject();          // lives on the pump thread; only used as an invoke target
        context_->moveToThread(thread_);
        thread_->start();
    }

    // A QThread only dispatches posted events if a QCoreApplication instance exists, so make sure
    // there is one. Deliberately lazy (first DSRGraph construction, not module import) and
    // deliberately conditional: a PySide/PyQt host has already built its own QApplication, and we
    // must reuse it rather than trip Qt's "instance already exists" fatal. In that case the host's
    // Qt loop drives the applies and the pump thread is just another loop alongside it.
    static void ensure_qapp()
    {
        if (QCoreApplication::instance() != nullptr) return;
        static int   argc  = 1;
        static char  arg0[] = "pydsr";
        static char *argv[] = {arg0, nullptr};
        new QCoreApplication(argc, argv);   // leaked on purpose; must outlive the pump thread
    }

    QThread *thread_{nullptr};
    QObject *context_{nullptr};
};

// Deleter that sends the DSRGraph back to the pump thread to be destroyed there.
struct GraphOnPumpThreadDeleter
{
    void operator()(DSRGraph *g) const
    {
        if (g == nullptr) return;
        py::gil_scoped_release release;    // the dtor joins DDS/graph threads; never hold the GIL
        GraphEventPump::instance().run_blocking([g] { delete g; });
    }
};


PYBIND11_MODULE(pydsr, m) {
    py::enum_<SyncMode>(m, "SyncMode")
        .value("CRDT", SyncMode::CRDT)
        .value("LWW", SyncMode::LWW)
        .export_values();

    py::bind_map<std::map<std::pair<uint64_t, std::string>, Edge>>(m, "MapStringEdge");
    py::bind_dsr_map<std::map<std::string, Attribute>>(m, "MapStringAttribute");

    m.doc() = "DSR Api for python";

    uint64_t local_agent_id = -1;

    //Disable messages from Qt.
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &context, const QString &msg) {
        if (type == QtCriticalMsg || type == QtFatalMsg) {
            fprintf(stderr, "%s", msg.toStdString().c_str());
        }
    });

    //Disable cout
    //m.attr("redirect_output") = py::capsule(new scoped_ostream_discard(),
    //                                        [](void *sor) { delete static_cast<scoped_ostream_discard *>(sor); });


    auto sig = m.def_submodule("signals",
    R""""(
    Connect functions to DSR signals. The types of the signals are defined in the "signal_type enum.

    In order to connect the signals you must annotate the types of function parameters to match those of the signals.
    The function signatures are the following:

    UPDATE_NODE: [[int, str], None]

    UPDATE_NODE_ATTR: [[int, [str]], None]

    UPDATE_EDGE: [[int, int, str], None]

    UPDATE_EDGE_ATTR: [[int, int, [str]], None]

    DELETE_EDGE: [[int, int, str], None]

    DELETE_NODE: [[int], None]

    DELETE_NODE_OBJ: [[pydsr.Node], None]

    DELETE_EDGE_OBJ: [[pydsr.EDGE], None]
    ")"""");

    enum signal_type
    {
        UPDATE_NODE,
        UPDATE_NODE_ATTR,
        UPDATE_EDGE,
        UPDATE_EDGE_ATTR,
        DELETE_EDGE,
        DELETE_NODE,
        DELETE_NODE_OBJ,
        DELETE_EDGE_OBJ
    };


    py::enum_<signal_type>(sig, "signal_type")
            .value("UPDATE_NODE", UPDATE_NODE)
            .value("UPDATE_NODE_ATTR", UPDATE_NODE_ATTR)
            .value("UPDATE_EDGE", UPDATE_EDGE)
            .value("UPDATE_EDGE_ATTR", UPDATE_EDGE_ATTR)
            .value("DELETE_EDGE", DELETE_EDGE)
            .value("DELETE_NODE", DELETE_NODE)
            .value("DELETE_EDGE_OBJ", DELETE_EDGE_OBJ)
            .value("DELETE_NODE_OBJ", DELETE_NODE_OBJ)
            .export_values();



    sig.def("connect", [&](DSRGraph *G, signal_type type, callback_types fn_callback) {

        auto runner = G->get_signal_runner();
        if (!runner) {
            throw std::runtime_error("Signal runner doesn't exist");
        }
        try {
            runner->connect(fn_callback, std::to_string(type));
        } catch (std::exception &e) {
            std::cout << "Update Node Callback must be (int, str)\n "  << std::endl;
            throw e;
        }
    });


    //DSR Attribute class
    py::class_<Attribute>(m, "Attribute")
            .def(py::init([&](attribute_type const& v, uint64_t t, uint32_t agent_id){
                    //Use another variant type to avoid problems with implicit conversions.
                    return Attribute(convert_variant(v), t, agent_id);
                }),
                 "value"_a, "timestamp"_a, "agent_id"_a)
            .def(py::init([&](attribute_type const& v , uint32_t agent_id) {
                //Comprobar tipos en Value. Como se convien los arrays de numpy, las listas, los doubles, etc.

                return Attribute(convert_variant(v), get_unix_timestamp(), agent_id);
            }),"value"_a, "agent_id"_a)
            .def(py::init([&](attribute_type const& v) {
                //Comprobar tipos en Value. Como se convien los arrays de numpy, las listas, los doubles, etc.
                return Attribute(convert_variant(v), get_unix_timestamp(), local_agent_id);
            }),"value"_a)
            .def("__repr__", [](Attribute const &self) {

                std::stringstream out;
                out << " < ";
                out << self;
                out << " >";
                return out.str();
            })
            .def_property_readonly("agent_id", [](Attribute &self) { return self.agent_id(); },
                                   "read the agent_id attribute. This property is readonly ans it is updated when a change is made in the value property.")
            .def_property_readonly("timestamp", [](Attribute &self) { return self.timestamp(); },
                                   "read the timestamp (ns) attribute. This property is readonly and it is updated when a change is made in the value property.")
            .def_property("value", [](Attribute &self) -> attribute_type {
                        switch (self.selected()) {
                            // Basic types
                            case 0: return self.str();
                            case 1: return self.dec();
                            case 2: return self.fl();
                            case 6: return self.uint();
                            case 7: return self.uint64();
                            case 8: return self.dob();
                            case 4: return self.bl();
                    
                            // Vectors types
                            case 3:  // float_vec
                                return py::array_t<float>(
                                    {(py::ssize_t)self.float_vec().size()},  // Shape
                                    {sizeof(float)},                         // Stride
                                    self.float_vec().data(),                  // Pointer
                                    py::cast(self)                            // Owner
                                );
                            case 5:  // byte_vec
                                return py::array_t<uint8_t>(
                                    {(py::ssize_t)self.byte_vec().size()},
                                    {sizeof(uint8_t)},
                                    self.byte_vec().data(),
                                    py::cast(self)
                                );
                            case 9:  // u64_vec
                                return py::array_t<uint64_t>(
                                    {(py::ssize_t)self.u64_vec().size()},
                                    {sizeof(uint64_t)},
                                    self.u64_vec().data(),
                                    py::cast(self)
                                );
                            case 10:  // vec2
                                return py::array_t<float>(
                                    {2}, {sizeof(float)},
                                    self.vec2().data(),
                                    py::cast(self)
                                );
                            case 11:  // vec3
                                return py::array_t<float>(
                                    {3}, {sizeof(float)},
                                    self.vec3().data(),
                                    py::cast(self)
                                );
                            case 12:  // vec4
                                return py::array_t<float>(
                                    {4}, {sizeof(float)},
                                    self.vec4().data(),
                                    py::cast(self)
                                );
                            case 13:  // vec6
                                return py::array_t<float>(
                                    {6}, {sizeof(float)},
                                    self.vec6().data(),
                                    py::cast(self)
                                );
                            default:
                                throw pybind11::type_error("Unreachable");
                        }
                },
                          [&](Attribute &self, const attribute_type &val) {
                              auto excep = std::string("Attributes cannot change type. Selected type is " + std::string{DSR::TYPENAMES_UNION[self.selected()]} + " and used type is " + std::string{attribute_type_TYPENAMES_UNION[val.index()]});
                              try {
                                  //std::cout << "[SET MAP VALUE] " << self.selected() << ", " << val.index() << std::endl;
                                  switch (self.selected()) {
                                      case 0:
                                          self.str(std::get<std::string>(val));
                                          break;
                                      case 1:
                                          if (val.index() == ATT_ENUM::U64_PY) self.dec(std::get<uint64_t>(val)); //This is intentional
                                          else self.dec(std::get<int32_t>(val));
                                          break;
                                      case 2:
                                          if (val.index() == ATT_ENUM::DOUBLE_PY) self.fl(std::get<double>(val)); //This is intentional
                                          else self.fl(std::get<float>(val));
                                          break;
                                      case 3:
                                          if (val.index() == ATT_ENUM::NPYF) {
                                              auto tmp = std::get<py::array_t<float>>(val);
                                              const auto size = tmp.size();
                                              py::buffer_info x = tmp.request();
                                              self.float_vec(std::vector<float>{static_cast<float *>(x.ptr), static_cast<float *>(x.ptr) + size});
                                          }
                                          else self.float_vec(std::get<std::vector<float>>(val));
                                          break;
                                      case 4:
                                          self.bl(std::get<no_int_cast_bool>(val)());
                                          break;
                                      case 5:
                                          if (val.index() == ATT_ENUM::NPYU8) {
                                              auto tmp = std::get<py::array_t<uint8_t>>(val);
                                              const auto size = tmp.size();
                                              py::buffer_info x = tmp.request();
                                              self.byte_vec(std::vector<uint8_t>{static_cast<uint8_t *>(x.ptr), static_cast<uint8_t *>(x.ptr) + size});
                                          }
                                          else self.byte_vec(std::get<std::vector<uint8_t>>(val));
                                          break;
                                      case 6:
                                          if (val.index() == ATT_ENUM::U64_PY) self.uint(std::get<uint64_t>(val));
                                          else self.uint(std::get<uint32_t>(val));
                                          break;
                                      case 7:
                                          self.uint64(std::get<uint64_t>(val));
                                          break;
                                      case 8:
                                          self.dob(std::get<double>(val));
                                          break;
                                      case 9:
                                          if (val.index() == ATT_ENUM::NPYU64) {
                                              auto tmp = std::get<py::array_t<uint64_t>>(val);
                                              const auto size = tmp.size();
                                              py::buffer_info x = tmp.request();
                                              self.u64_vec(std::vector<uint64_t>{static_cast<uint64_t *>(x.ptr), static_cast<uint64_t *>(x.ptr) + size});
                                          } else if (val.index() == ATT_ENUM::VECU8_PY) {
                                              auto tmp = std::get<std::vector<uint8_t>>(val);
                                              self.u64_vec(std::vector<uint64_t>{tmp.begin(), tmp.end()});
                                          }
                                          else self.u64_vec(std::get<std::vector<uint64_t>>(val));
                                          break;
                                      case 10:
                                          if (val.index() == ATT_ENUM::NPYF) {
                                              auto tmp = std::get<py::array_t<float>>(val);
                                              if (tmp.size() == 2) {
                                                  py::buffer_info x = tmp.request();
                                                  self.vec2(std::array<float, 2>{static_cast<float *>(x.ptr)[0],
                                                                                 static_cast<float *>(x.ptr)[1]});

                                              } else throw;
                                          }else if (val.index() == ATT_ENUM::VECF_PY) {
                                              auto tmp = std::get<std::vector<float>>(val);
                                              if (tmp.size() == 2) {
                                                  self.vec2(std::array<float, 2>{tmp[0], tmp[1]});
                                              } else throw pybind11::type_error(excep);
                                          }
                                          else self.vec2(std::get<std::array<float, 2>>(val));
                                          break;
                                      case 11:
                                          if (val.index() == ATT_ENUM::NPYF) {
                                              auto tmp = std::get<py::array_t<float>>(val);
                                              if (tmp.size() == 3) {
                                                  py::buffer_info x = tmp.request();
                                                  self.vec3(std::array<float, 3>{static_cast<float *>(x.ptr)[0],
                                                                                 static_cast<float *>(x.ptr)[1],
                                                                                 static_cast<float *>(x.ptr)[2]});

                                              } else throw pybind11::type_error(excep);
                                          } else if (val.index() == ATT_ENUM::VECF_PY) {
                                              auto tmp = std::get<std::vector<float>>(val);
                                              if (tmp.size() == 3) {
                                                  self.vec3(std::array<float, 3>{tmp[0],
                                                                                 tmp[1],
                                                                                 tmp[2]});
                                              } else throw pybind11::type_error(excep);

                                          }
                                          else self.vec3(std::get<std::array<float, 3>>(val));
                                          break;
                                      case 12:
                                          if (val.index() == ATT_ENUM::NPYF) {
                                              auto tmp = std::get<py::array_t<float>>(val);
                                              if (tmp.size() == 4) {
                                                  py::buffer_info x = tmp.request();
                                                  self.vec4(std::array<float, 4>{static_cast<float *>(x.ptr)[0],
                                                                                 static_cast<float *>(x.ptr)[1],
                                                                                 static_cast<float *>(x.ptr)[2],
                                                                                 static_cast<float *>(x.ptr)[3]});

                                              } else throw pybind11::type_error(excep);
                                          }else if (val.index() == ATT_ENUM::VECF_PY) {
                                              auto tmp = std::get<std::vector<float>>(val);
                                              if (tmp.size() == 4) {
                                                  self.vec4(std::array<float, 4>{tmp[0],
                                                                                 tmp[1],
                                                                                 tmp[2],
                                                                                 tmp[3]});
                                              } else throw pybind11::type_error(excep);
                                          }
                                          else self.vec4(std::get<std::array<float, 4>>(val));
                                          break;
                                      case 13:
                                          if (val.index() == ATT_ENUM::NPYF) {
                                              auto tmp = std::get<py::array_t<float>>(val);
                                              if (tmp.size() == 6) {
                                                  py::buffer_info x = tmp.request();
                                                  self.vec6(std::array<float, 6>{static_cast<float *>(x.ptr)[0],
                                                                                 static_cast<float *>(x.ptr)[1],
                                                                                 static_cast<float *>(x.ptr)[2],
                                                                                 static_cast<float *>(x.ptr)[3],
                                                                                 static_cast<float *>(x.ptr)[4],
                                                                                 static_cast<float *>(x.ptr)[5]});

                                              } else throw pybind11::type_error(excep);
                                          } else if (val.index() == ATT_ENUM::VECF_PY) {
                                              auto tmp = std::get<std::vector<float>>(val);
                                              if (tmp.size() == 6) {
                                                  self.vec6(std::array<float, 6>{tmp[0],
                                                                                 tmp[1],
                                                                                 tmp[2],
                                                                                 tmp[3],
                                                                                 tmp[4],
                                                                                 tmp[5]});
                                              } else throw pybind11::type_error(excep);
                                          }
                                          else self.vec6(std::get<std::array<float, 6>>(val));
                                          break;
                                      default:
                                          throw /*std::runtime_error*/ pybind11::type_error(excep);
                                      };
                                      self.timestamp(get_unix_timestamp());
                                      self.agent_id(local_agent_id);
                              } catch (...) {
                                  throw /*std::runtime_error*/ pybind11::type_error(excep);
                              }
                          },
                          py::return_value_policy::reference, "read or assign a new value to the Attribute object.");

    //DSR Edge class
    py::class_<Edge>(m, "Edge")
            .def(py::init<uint64_t, uint64_t, std::string, uint32_t>(),
                 "to"_a, "from"_a, "type"_a, "agent_id"_a)
            .def("__repr__", [](Edge const &self) {
                std::stringstream out;
                out << "------------------------------------" << std::endl;
                out << "Type: " << self.type() << " from: " << self.from() << " to: " << self.to() << std::endl;
                for (auto& [k, v] : self.attrs())
                    out << "      [" << k << "] -- " << v << std::endl;
                out << "------------------------------------" << std::endl;
                return out.str();
            })
            .def_property_readonly("type", [](Edge const &self) { return self.type(); },
                                   "read the type of the edge. This property is readonly")
            .def_property_readonly("origin", [](Edge const &self) { return self.from(); },
                                   "read the origen node of the edge. This property is readonly")
            .def_property_readonly("destination", [](Edge const &self) { return self.to(); },
                                   "read the destination node of the edge. This property is readonly")
            .def_property("agent_id", [](Edge const &self) { return self.agent_id(); },
                          [](Edge &self, uint32_t val) { self.agent_id(val); },
                          "read or assign a new value to the agent_id attribute.")

            .def_property("attrs", [](Edge &self) -> std::map<std::string, Attribute> & { return self.attrs(); },
                          [](Edge &self, const std::map<std::string, Attribute> &at) { self.attrs(at); },
                          py::return_value_policy::reference, "read or write in the attribute map of the edge.");



    //DSR Node class
    py::class_<Node>(m, "Node")
            .def(py::init([](uint32_t agent_id, const std::string &type,
                             const std::string &name = "") -> std::unique_ptr<Node> {
                auto tmp = std::make_unique<Node>(agent_id, type);
                tmp->name(name);
                return tmp;
            }), "agent_id"_a, "type"_a, "name"_a = "")
            .def("__repr__", [](Node const &self) {
                std::stringstream out;

                out << "------------------------------------" << std::endl;
                out << "Node: " << self.id() << std::endl;
                out << "  Type: " << self.type() << std::endl;
                out << "  Name: " << self.name() << std::endl;
                out << "  Agent id: " << self.agent_id() << std::endl;
                out << "  ATTRIBUTES: [\n";
                for (auto& [key, val] : self.attrs())
                    out << "      [" << key << "] -- " << val << std::endl;
                out << "   ]" << std::endl;
                out << "  EDGES: [" << std::endl;
                for (auto& [key, val] : self.fano()) {
                    out << "          Edge type: " << val.type() << " from: " << val.from() << " to: " << val.to()
                        << std::endl;
                    for (auto& [k, v] : val.attrs())
                        out << "              [" << k << "] -- " << v << std::endl;
                }
                out << "          ]" << std::endl;
                out << "------------------------------------" << std::endl;

                return out.str();
            })
            .def_property_readonly("id", [](Node const &self) { return self.id(); },
                                   "read the id of the node. This property is readonly and is generated by the idserver agent when the node is inserted")
            .def_property_readonly("name", [](Node const &self) { return self.name(); },
                                   "read the name of the node. This property is readonly. If the name is not provided to the constructor or the name already exist in G, the name is generated by the idserver agent with a combination of the type and the id when the node is inserted")
            .def_property_readonly("type", [](Node const &self) { return self.type(); },
                                   "read the type of the node. This property is readonly")
            .def_property("agent_id", [](Node const &self) { return self.agent_id(); },
                          [](Node &self, uint32_t val) { self.agent_id(val); },
                          "read or assign a new value to the agent_id attribute.")
            .def_property("attrs", [](Node &self) -> std::map<std::string, Attribute> & { return self.attrs(); },
                          [](Node &self, const std::map<std::string, Attribute> &at) { self.attrs(at); },
                          py::return_value_policy::reference, "read or write in the attribute map of the node.")
            .def_property("edges",
                          [](Node &self) -> std::map<std::pair<uint64_t, std::string>, Edge> & { return self.fano(); },
                          [](Node &self, const std::map<std::pair<uint64_t, std::string>, Edge> &edges) {
                              return self.fano(edges);
                          },
                          py::return_value_policy::reference, "read or write in the edge map of the node.")
            .def("get_edges", [](Node &self){
                std::vector<Edge> edges;
                edges.reserve(self.fano().size());
                for (auto [_, edge] : self.fano()) {
                    edges.emplace_back(edge);
                }
                return edges;
            });



    //DSR DSRGraph class
    py::class_<DSRGraph, std::unique_ptr<DSRGraph, GraphOnPumpThreadDeleter>>(m, "DSRGraph")
            .def(py::init([&](int root, const std::string &name, int id,
                              const std::string &dsr_input_file = "",
                              bool all_same_host = true, int8_t domain_id = 0,
                              SyncMode sync_mode = SyncMode::CRDT)
                              -> std::unique_ptr<DSRGraph, GraphOnPumpThreadDeleter> {
                     local_agent_id = id;
                     GraphSettings settings;
                     settings.agent_id = id;
                     settings.graph_name = name;
                     settings.input_file = dsr_input_file;
                     settings.same_host = all_same_host;
                     settings.domain_id = domain_id;
                     settings.signal_mode = SignalMode::Queue;
                     settings.sync_mode = sync_mode;
                     // Construct ON the pump thread so the graph's Qt affinity is that thread from
                     // the very first posted event -- the full-graph answer already lands during the
                     // constructor's own sync wait. (The constructor blocks the pump's event loop
                     // while it waits; the queued applies simply run as soon as it returns.)
                     DSRGraph *raw = nullptr;
                     GraphEventPump::instance().run_blocking(
                             [&] { raw = new DSRGraph(settings); });
                     std::unique_ptr<DSRGraph, GraphOnPumpThreadDeleter> g(raw);
                     return g;
                 }), "root"_a, "name"_a, "id"_a, "dsr_input_file"_a = "",
                 "all_same_host"_a = true, "domain_id"_a=0, "sync_mode"_a = SyncMode::CRDT, py::call_guard<py::gil_scoped_release>())
            .def("get_agent_id", &DSRGraph::get_agent_id, "get agent_id")
            .def("get_agent_name", &DSRGraph::get_agent_name, "get agent_id")
            .def("get_node", [](DSRGraph &self, uint64_t id) -> std::optional<Node> {
                return self.get_node(id);
            }, "id"_a, "return the node with the id passed as parameter. Returns None if the node does not exist.")
            .def("get_node", [](DSRGraph &self, const std::string &name) -> std::optional<Node> {
                return self.get_node(name);
            }, "name"_a, "return the node with the name passed as parameter. Returns None if the node does not exist.")
            .def("delete_node", static_cast<bool (DSRGraph::*)(uint64_t)>(&DSRGraph::delete_node), "id"_a,
                 "delete the node with the given id. Returns a bool with the result o the operation.")
            .def("delete_node",
                 static_cast<bool (DSRGraph::*)(const std::basic_string<char> &)>(&DSRGraph::delete_node), "name"_a,
                 "delete the node with the given name. Returns a bool with the result o the operation.")
            .def("insert_node", [](DSRGraph &g, Node &n) -> std::optional<uint64_t> {
                     return g.insert_node(n);
                 }, "node"_a,
                 "Insert in the graph the new node passed as parameter. Returns the id of the node or None if the Node alredy exist in the map.")
            .def("update_node", &DSRGraph::update_node<DSR::Node&>, "node"_a, "Update the node in the graph. Returns a bool.")
            .def("get_edge", [](DSRGraph &self, const std::string &from, const std::string &to,
                                const std::string &key) -> std::optional<Edge> {
                     return self.get_edge(from, to, key);
                 }, "from"_a, "to"_a, "type"_a,
                 "Return the edge with the parameters from, to, and type passed as parameter. If the edge does not exist it return None")
            .def("get_edge",
                 [](DSRGraph &self, uint64_t from, uint64_t to, const std::string &key) -> std::optional<Edge> {
                     return self.get_edge(from, to, key);
                 }, "from"_a, "to"_a, "type"_a,
                 "Return the edge with the parameters from, to, and type passed as parameter.  If the edge does not exist it return None")
            .def("insert_or_assign_edge", &DSRGraph::insert_or_assign_edge<DSR::Edge&>, "edge"_a,
                 "Insert or updates and edge. returns a bool")
            .def("delete_edge", static_cast<bool (DSRGraph::*)(uint64_t, uint64_t,
                                                               const std::basic_string<char> &)>(&DSRGraph::delete_edge),
                 "from"_a, "to"_a, "type"_a, "Removes the edge and returns a bool")
            .def("delete_edge",
                 static_cast<bool (DSRGraph::*)(const std::basic_string<char> &, const std::basic_string<char> &,
                                                const std::basic_string<char> &)>(&DSRGraph::delete_edge), "from"_a,
                 "to"_a, "type"_a, "Removes the edge and returns a bool")

            .def("get_node_root", &DSRGraph::get_node_root, "Return the root node.")
            .def("get_nodes_by_type", &DSRGraph::get_nodes_by_type, "type"_a, "Return all the nodes with a given type.")
            .def("get_nodes", &DSRGraph::get_nodes, "Returns all nodes")
            .def("get_name_from_id", &DSRGraph::get_name_from_id, "id"_a, "Return the name of a node given its id")
            .def("get_id_from_name", &DSRGraph::get_id_from_name, "name"_a, "Return the id from a node given its name")
            .def("get_edges", &DSRGraph::get_edges, "Return all the edges in the graph")
            .def("get_edges_by_type", &DSRGraph::get_edges_by_type, "type"_a, "Return all the edges with a given type.")
            .def("get_edges_to_id", &DSRGraph::get_edges_to_id, "id"_a, "Return all the edges that point to the node")
            .def("write_to_json_file", &DSRGraph::write_to_json_file, "file"_a, "skip_atts"_a=std::vector<std::string>{}, "Return all the edges that point to the node");
    //DSR RT_API class
        auto rt_api = py::class_<RT_API>(m, "rt_api");
        py::enum_<RT_API::TimeQuery>(rt_api, "time_query")
            .value("nearest", RT_API::TimeQuery::Nearest)
            .value("interpolated", RT_API::TimeQuery::Interpolated);
        py::enum_<RT_API::CovarianceKind>(rt_api, "covariance_kind")
            .value("pose", RT_API::CovarianceKind::Pose)
            .value("velocity", RT_API::CovarianceKind::Velocity)
            .value("acceleration", RT_API::CovarianceKind::Acceleration);

        rt_api
            .def(py::init([](DSRGraph &g) -> std::unique_ptr<RT_API> {
                return g.get_rt_api();
            }))
            .def("insert_or_assign_edge_RT", [](RT_API &self, Node &n, uint64_t to,
                                                const std::vector<float> &translation,
                                                const std::vector<float> &rotation_euler,
                                                std::optional<uint64_t> timestamp
            ) {
                self.insert_or_assign_edge_RT(n, to, translation, rotation_euler, timestamp);
            }, "node"_a, "to"_a, "trans"_a, "rot_euler"_a, "timestamp"_a=std::nullopt)
            .def_static("get_edge_RT", &RT_API::get_edge_RT, "node"_a, "to"_a, "type_edge"_a="RT")
            .def("get_RT_pose_from_parent", [](RT_API &self, Node &e, const std::string &type_edge) -> std::optional<Eigen::Matrix<double, 4, 4>> {
                auto tmp = self.get_RT_pose_from_parent(e);
                if (tmp.has_value()) {
                    Eigen::Matrix<double, 4, 4> Trans;
                    Trans.setIdentity();   // Set to Identity to make bottom row of Matrix 0,0,0,1
                    Trans.block<3, 3>(0, 0) = tmp.value().rotation();
                    Trans.block<3, 1>(0, 3) = tmp.value().translation();
                    return Trans;
                } else {
                    return std::nullopt;
                }
            }, "node"_a, "type_edge"_a="RT")
            .def("get_edge_RT_as_rtmat", [](RT_API &self, Edge &e, std::uint64_t t, RT_API::TimeQuery time_query) -> std::optional<Eigen::Matrix<double, 4, 4>> {
                auto tmp = self.get_edge_RT_as_rtmat(e, t, time_query);
                if (tmp.has_value()) {
                    Eigen::Matrix<double, 4, 4> Trans;
                    Trans.setIdentity();   // Set to Identity to make bottom row of Matrix 0,0,0,1
                    Trans.block<3, 3>(0, 0) = tmp.value().rotation();
                    Trans.block<3, 1>(0, 3) = tmp.value().translation();
                    return Trans;
                } else {
                    return std::nullopt;
                }
            }, "edge"_a, "timestamp"_a=0, "time_query"_a=RT_API::TimeQuery::Nearest)
            .def("get_translation", [](RT_API &self, std::uint64_t node_id, std::uint64_t to, std::uint64_t timestamp, RT_API::TimeQuery time_query)
            {
                return self.get_translation(node_id, to, timestamp, time_query);
            }, "node_id"_a, "to"_a, "timestamp"_a=0, "time_query"_a=RT_API::TimeQuery::Nearest)
            // Covariance is stored as a 6x6 row-major SE(3) block over [x,y,z,rx,ry,rz], possibly
            // inside a history ring — read it through here rather than reshaping the raw attribute,
            // which would hand back HISTORY_SIZE stacked blocks.
            .def("get_covariance_matrix", [](RT_API &self, std::uint64_t node_id, std::uint64_t to,
                                             std::uint64_t timestamp, RT_API::TimeQuery time_query,
                                             RT_API::CovarianceKind kind)
            {
                return self.get_covariance_matrix(node_id, to, timestamp, time_query, kind);
            }, "node_id"_a, "to"_a, "timestamp"_a=0, "time_query"_a=RT_API::TimeQuery::Nearest,
               "kind"_a=RT_API::CovarianceKind::Pose)
            .def("insert_or_assign_edge_RT_covariance", [](RT_API &self, std::uint64_t node_id, std::uint64_t to,
                                                           RT_API::CovarianceKind kind,
                                                           const std::vector<float> &covariance,
                                                           std::optional<uint64_t> timestamp)
            {
                return self.insert_or_assign_edge_RT_covariance(node_id, to, kind, covariance, timestamp);
            }, "node_id"_a, "to"_a, "kind"_a, "covariance"_a, "timestamp"_a=std::nullopt);

    py::class_<InnerEigenAPI>(m, "inner_api")
            .def(py::init([](DSRGraph &g) -> std::unique_ptr<InnerEigenAPI> {
                return g.get_inner_eigen_api();
            }), "graph"_a)
            .def("transform", static_cast<std::optional<Eigen::Vector3d> (InnerEigenAPI::*)(const std::string &,
                                                                                            const std::string &,
                                                                                            std::uint64_t timestamp,
                                                                              const std::string &type_edge,
                                                                              RT_API::TimeQuery time_query)>(&InnerEigenAPI::transform),
                  "orig"_a, "dest"_a, "timestamp"_a=0, "type_edge"_a="RT", "time_query"_a=RT_API::TimeQuery::Nearest)
            .def("transform", static_cast<std::optional<Eigen::Vector3d> (InnerEigenAPI::*)(const std::string &,
                                                                                            const Mat::Vector3d &,
                                                                                            const std::string &,
                                                                                            std::uint64_t timestamp,
                                                                              const std::string &type_edge,
                                                                              RT_API::TimeQuery time_query)>(&InnerEigenAPI::transform),
                  "orig"_a, "vector"_a, "dest"_a, "timestamp"_a=0, "type_edge"_a="RT", "time_query"_a=RT_API::TimeQuery::Nearest)

            .def("transform_axis", static_cast<std::optional<Mat::Vector6d> (InnerEigenAPI::*)(const std::string &,
                                                                                               const std::string &,
                                                                                               std::uint64_t timestamp,
                                                                                const std::string &type_edge,
                                                                                RT_API::TimeQuery time_query)>(&InnerEigenAPI::transform_axis),
                  "orig"_a, "dest"_a, "timestamp"_a=0, "type_edge"_a="RT", "time_query"_a=RT_API::TimeQuery::Nearest)
            .def("transform_axis",
                 static_cast<std::optional<Mat::Vector6d> (InnerEigenAPI::*)(const std::string &, const Mat::Vector6d &,
                                                                             const std::string &,
                                                                             std::uint64_t timestamp,
                                                                  const std::string &type_edge,
                                                                  RT_API::TimeQuery time_query)>(&InnerEigenAPI::transform_axis),
                  "orig"_a, "vector"_a, "dest"_a, "timestamp"_a=0, "type_edge"_a="RT", "time_query"_a=RT_API::TimeQuery::Nearest)


            .def("get_transformation_matrix",
                 [](InnerEigenAPI &self, const std::string &dest,
                    const std::string &orig, std::uint64_t timestamp, const std::string &type_edge, RT_API::TimeQuery time_query) -> std::optional<Eigen::Matrix<double, 4, 4>> {
                     auto tmp = self.get_transformation_matrix(dest, orig, timestamp, type_edge, time_query);
                     if (tmp.has_value()) {
                         Eigen::Matrix<double, 4, 4> Trans;
                         Trans.setIdentity();   // Set to Identity to make bottom row of Matrix 0,0,0,1
                         Trans.block<3, 3>(0, 0) = tmp.value().rotation();
                         Trans.block<3, 1>(0, 3) = tmp.value().translation();
                         return Trans;
                     } else {
                         return std::nullopt;
                     }
                 }, "orig"_a, "dest"_a, "timestamp"_a=0, "type_edge"_a="RT", "time_query"_a=RT_API::TimeQuery::Nearest)
            .def("get_rotation_matrix", &InnerEigenAPI::get_rotation_matrix, "orig"_a, "dest"_a, "timestamp"_a=0, "type_edge"_a="RT", "time_query"_a=RT_API::TimeQuery::Nearest)
            .def("get_translation_vector", &InnerEigenAPI::get_translation_vector, "orig"_a, "dest"_a, "timestamp"_a=0, "type_edge"_a="RT", "time_query"_a=RT_API::TimeQuery::Nearest)
            .def("get_euler_xyz_angles", &InnerEigenAPI::get_euler_xyz_angles, "orig"_a, "dest"_a, "timestamp"_a=0, "type_edge"_a="RT", "time_query"_a=RT_API::TimeQuery::Nearest);


    bind_ghistory(m);
    /*
    py::class_<CameraAPI>(m, "camera_api")
            .def(py::init([](DSRGraph &g, Node &cam) -> std::unique_ptr<CameraAPI> {
                return g.get_camera_api(cam);
            }))
            .def_property_readonly("id", [](CameraAPI& self) { return self.get_id();})
            .def_property_readonly("focal", [](CameraAPI& self) { return self.get_focal();})
            .def_property_readonly("focal_x", [](CameraAPI& self) { return self.get_focal_x();})
            .def_property_readonly("focal_y", [](CameraAPI& self) { return self.get_focal_y();})
            .def_property_readonly("height", [](CameraAPI& self) { return self.get_height();})
            .def_property_readonly("width", [](CameraAPI& self) { return self.get_width();})
            .def("get_roi_depth", &CameraAPI::get_roi_depth)
            .def("project", &CameraAPI::project)

            .def("reload_camera", &CameraAPI::reload_camera)
            .def("get_rgb_image", &CameraAPI::get_rgb_image)
            .def("get_depth_image",
                    static_cast<std::optional<std::vector<float>> (CameraAPI::*)()>(&CameraAPI::get_depth_image))
            .def("get_depth_image",
                 static_cast<std::optional<std::reference_wrapper<const std::vector<uint8_t>>> (CameraAPI::*)() const>(&CameraAPI::get_depth_image))
            .def("get_pointcloud", &CameraAPI::get_pointcloud)
            .def("get_depth_as_gray_image", &CameraAPI::get_depth_as_gray_image)

    ;
    */
}
