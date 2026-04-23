
#include "dsr/api/dsr_api.h"
#include "../utils.h"
#include <thread>
#include <QtWidgets/QApplication>

#include "catch2/catch_test_macros.hpp"
#include "catch2/generators/catch_generators.hpp"
#include "catch2/internal/catch_test_registry.hpp"

using namespace DSR;


/*
        void update_node_signal(uint64_t, const std::string &type, SignalInfo info = {});
        void update_node_attr_signal(uint64_t id ,const std::vector<std::string>& att_names, SignalInfo info = {});

        void update_edge_signal(uint64_t from, uint64_t to, const std::string &type, SignalInfo info = {});
        void update_edge_attr_signal(uint64_t from, uint64_t to, const std::string &type, const std::vector<std::string>& att_name, SignalInfo info = {});

        void del_edge_signal(uint64_t from, uint64_t to, const std::string &edge_tag, SignalInfo info = {});
        void deleted_edge_signal(const DSR::Edge & edge);
        void del_node_signal(uint64_t id, SignalInfo info = {}) ;
        void deleted_node_signal(const DSR::Node & edge);
 */

struct QTestApplication: public QCoreApplication {
    QTestApplication(int &argc, char **argv) : QCoreApplication(argc, argv) {}
    
    bool notify(QObject* receiver, QEvent* event) override {
        bool done = true;
        try {
            done = QCoreApplication::notify(receiver, event);
        } catch (std::exception& e) {
            std::cerr << "----------------------------------------------------------\nGot an exception:\n";
            std::cerr << e.what();
            std::cerr << "\n----------------------------------------------------------\n";
            throw e;
         }
        catch (std::string e) {
             std::cerr << "----------------------------------------------------------\nGot an exception:\n";
             std::cerr << e;
             std::cerr << "\n----------------------------------------------------------\n";
             throw e;
        }
        catch (...) {
            std::cerr << "----------------------------------------------------------\nOther error\n";
            std::cerr << "\n----------------------------------------------------------\n";
            throw std::current_exception();
        }
        return done;
    }
};

TEST_CASE("Insert a node without attributes and edges", "[GRAPH][SIGNALS]"){
    const auto sync_mode = GENERATE(SyncMode::CRDT, SyncMode::LWW);
    CAPTURE(sync_mode_label(sync_mode));
    auto ctx = make_empty_config_file();
    auto id1 = rand() % 1000;
    int argc = 0;
    QTestApplication app(argc, nullptr); // need this to trigger signals
    DSRGraph G(make_test_graph_settings(random_string(10), id1, ctx, true, 0, SignalMode::QT, sync_mode));

    bool update_node_signal_recv = false;
    QObject::connect(&G, &DSRGraph::update_node_signal, &app,
                     [&](uint64_t, const std::string &type, SignalInfo info) {
                         update_node_signal_recv = true;
                     },
                     Qt::QueuedConnection);


    bool update_edge_signal_recv = false;
    QObject::connect(&G, &DSRGraph::update_edge_signal, &app,
                     [&](uint64_t from, uint64_t to, const std::string &type, SignalInfo info) {
                         update_edge_signal_recv = true;
                     },
                     Qt::QueuedConnection);

    bool update_node_attr_signal_recv = false;
    QObject::connect(&G, &DSRGraph::update_node_attr_signal, &app,
                 [&](uint64_t id ,const std::vector<std::string>& att_names, SignalInfo info) {
                     update_node_attr_signal_recv = true;
                 },
                 Qt::QueuedConnection);

    bool update_edge_attr_signal_recv = false;
    QObject::connect(&G, &DSRGraph::update_edge_attr_signal, &app,
                 [&](uint64_t from, uint64_t to, const std::string &type, const std::vector<std::string>& att_name, SignalInfo info) {
                     update_edge_attr_signal_recv = true;
                 },
                 Qt::QueuedConnection);

    bool del_edge_signal_recv = false;
    QObject::connect(&G, &DSRGraph::del_edge_signal, &app,
                 [&](uint64_t from, uint64_t to, const std::string &edge_tag, SignalInfo info) {
                     del_edge_signal_recv = true;
                 },
                 Qt::QueuedConnection);

    bool del_node_signal_recv = false;
    QObject::connect(&G, &DSRGraph::del_node_signal, &app,
                 [&](uint64_t from, SignalInfo info) {
                     del_node_signal_recv = true;
                 },
                 Qt::QueuedConnection);

    bool deleted_edge_signal_recv = false;
    QObject::connect(&G, &DSRGraph::deleted_edge_signal, &app,
                 [&](const DSR::Edge & edge) {
                     del_edge_signal_recv = true;
                 },
                 Qt::QueuedConnection);

    bool deleted_node_signal_recv = false;
    QObject::connect(&G, &DSRGraph::deleted_node_signal, &app,
                 [&](const DSR::Node & edge) {
                     del_node_signal_recv = true;
                 },
                 Qt::QueuedConnection);

    auto node_name = random_string();
    auto n = Node::create<testtype_node_type>(node_name);
    const std::optional<uint64_t> r  = G.insert_node(n);
    REQUIRE(r.has_value());

    QTimer::singleShot(0, [&]() {
        REQUIRE(update_node_signal_recv);
        REQUIRE(not update_edge_signal_recv);
        REQUIRE(not update_node_attr_signal_recv);
        REQUIRE(not update_edge_attr_signal_recv);
        REQUIRE(not del_node_signal_recv);
        REQUIRE(not del_edge_signal_recv);
        REQUIRE(not deleted_node_signal_recv);
        REQUIRE(not deleted_edge_signal_recv);
        QTestApplication::exit();
    });
    QTestApplication::exec();
}

static const auto new_attribute_ = []() -> std::pair<std::string, Attribute> {

    const auto val = random_choose(std::vector<ValType>{
        (int)12,
        random_string(),
        std::vector<float>{1.0, 2.0, 3.0}
    });
    Attribute attr(val, random_number(), static_cast<uint32_t>(random_number()));
    return std::make_pair(random_string(), attr);
};

static const auto new_edge_ = [](uint64_t to, const std::map<std::string, Attribute> &attributes = {}) -> std::pair<std::pair<uint64_t, std::string>, Edge> {

    auto type = random_choose(std::vector<std::string>{"in", "RT", "reachable", "visible"});
    auto key = std::pair{to, type};
    auto edge = Edge();
    edge.to(to);
    //edge.from(from);
    edge.type(type);
    edge.agent_id(random_number());
    edge.attrs(attributes);
    return std::make_pair(key, edge);
};

TEST_CASE("Insert a node with attributes and edges", "[GRAPH][SIGNALS]"){
    const auto sync_mode = GENERATE(SyncMode::CRDT, SyncMode::LWW);
    CAPTURE(sync_mode_label(sync_mode));
    auto ctx = make_empty_config_file();
    auto id1 = rand() % 1000;
    int argc = 0;
    QTestApplication app(argc, nullptr); // need this to trigger signals
    DSRGraph G(make_test_graph_settings(random_string(10), id1, ctx, true, 0, SignalMode::QT, sync_mode));
    auto node_name = random_string();
    auto n = Node::create<testtype_node_type>(node_name);
    std::optional<uint64_t> r  = G.insert_node(n);
    REQUIRE(r.has_value());

    bool update_node_signal_recv = false;
    QObject::connect(&G, &DSRGraph::update_node_signal, &app,
                     [&](uint64_t, const std::string &type, SignalInfo info) {
                         update_node_signal_recv = true;
                     },
                     Qt::QueuedConnection);


    int update_edge_signal_recv = 0;
    QObject::connect(&G, &DSRGraph::update_edge_signal, &app,
                     [&](uint64_t from, uint64_t to, const std::string &type, SignalInfo info) {
                         update_edge_signal_recv++;
                     },
                     Qt::QueuedConnection);

    int update_node_attr_signal_recv = 0;
    QObject::connect(&G, &DSRGraph::update_node_attr_signal, &app,
                 [&](uint64_t id ,const std::vector<std::string>& att_names, SignalInfo info) {
                     update_node_attr_signal_recv++;
                 },
                 Qt::QueuedConnection);

    int update_edge_attr_signal_recv = 0;
    QObject::connect(&G, &DSRGraph::update_edge_attr_signal, &app,
                 [&](uint64_t from, uint64_t to, const std::string &type, const std::vector<std::string>& att_name, SignalInfo info) {
                     update_edge_attr_signal_recv++;
                 },
                 Qt::QueuedConnection);

    bool del_edge_signal_recv = false;
    QObject::connect(&G, &DSRGraph::del_edge_signal, &app,
                 [&](uint64_t from, uint64_t to, const std::string &edge_tag, SignalInfo info) {
                     del_edge_signal_recv = true;
                 },
                 Qt::QueuedConnection);

    bool del_node_signal_recv = false;
    QObject::connect(&G, &DSRGraph::del_node_signal, &app,
                 [&](uint64_t from, SignalInfo info) {
                     del_node_signal_recv = true;
                 },
                 Qt::QueuedConnection);

    bool deleted_edge_signal_recv = false;
    QObject::connect(&G, &DSRGraph::deleted_edge_signal, &app,
                 [&](const DSR::Edge & edge) {
                     del_edge_signal_recv = true;
                 },
                 Qt::QueuedConnection);

    bool deleted_node_signal_recv = false;
    QObject::connect(&G, &DSRGraph::deleted_node_signal, &app,
                 [&](const DSR::Node & edge) {
                     del_node_signal_recv = true;
                 },
                 Qt::QueuedConnection);

    node_name = random_string();
    n = Node::create<testtype_node_type>(
        { new_attribute_(), new_attribute_(), new_attribute_()},
        { new_edge_(*r, {}), new_edge_(*r, {}), new_edge_(*r, {})}, node_name);
    const auto numedges = n.fano().size();
    std::optional<uint64_t> r2  = G.insert_node(n);
    REQUIRE(r2.has_value());

    QTimer::singleShot(0, [&]() {
        REQUIRE(update_node_signal_recv);
        REQUIRE(update_edge_signal_recv == numedges);
        REQUIRE(update_node_attr_signal_recv == 0);
        REQUIRE(update_edge_attr_signal_recv == 0);
        REQUIRE(not del_node_signal_recv);
        REQUIRE(not del_edge_signal_recv);
        REQUIRE(not deleted_node_signal_recv);
        REQUIRE(not deleted_edge_signal_recv);
        app.exit();
    });
    app.exec();
}


TEST_CASE("Update a node, add and remove attributes", "[GRAPH][SIGNALS]") {
    const auto sync_mode = GENERATE(SyncMode::CRDT, SyncMode::LWW);
    CAPTURE(sync_mode_label(sync_mode));
    auto ctx = make_empty_config_file();
    auto id1 = rand() % 1000;
    int argc = 0;
    QTestApplication app(argc, nullptr); // need this to trigger signals
    DSRGraph G(make_test_graph_settings(random_string(10), id1, ctx, true, 0, SignalMode::QT, sync_mode));
    auto node_name = random_string();
    auto n = Node::create<testtype_node_type>(node_name);
    n.attrs({new_attribute_(), new_attribute_(), new_attribute_()});
    std::optional<uint64_t> r  = G.insert_node(n);
    REQUIRE(r.has_value());

    bool update_node_signal_recv = false;
    QObject::connect(&G, &DSRGraph::update_node_signal, &app,
                     [&](uint64_t, const std::string &type, SignalInfo info) {
                         update_node_signal_recv = true;
                     },
                     Qt::QueuedConnection);


    int update_edge_signal_recv = 0;
    QObject::connect(&G, &DSRGraph::update_edge_signal, &app,
                     [&](uint64_t from, uint64_t to, const std::string &type, SignalInfo info) {
                         update_edge_signal_recv++;
                     },
                     Qt::QueuedConnection);

    int update_node_attr_signal_recv = 0;
    int update_node_attr_signal_size_recv = 0;
    QObject::connect(&G, &DSRGraph::update_node_attr_signal, &app,
                 [&](uint64_t id ,const std::vector<std::string>& att_names, SignalInfo info) {
                     update_node_attr_signal_recv++;
                     update_node_attr_signal_size_recv = att_names.size();
                 },
                 Qt::QueuedConnection);

    int update_edge_attr_signal_recv = 0;
    QObject::connect(&G, &DSRGraph::update_edge_attr_signal, &app,
                 [&](uint64_t from, uint64_t to, const std::string &type, const std::vector<std::string>& att_name, SignalInfo info) {
                     update_edge_attr_signal_recv++;
                 },
                 Qt::QueuedConnection);

    bool del_edge_signal_recv = false;
    QObject::connect(&G, &DSRGraph::del_edge_signal, &app,
                 [&](uint64_t from, uint64_t to, const std::string &edge_tag, SignalInfo info) {
                     del_edge_signal_recv = true;
                 },
                 Qt::QueuedConnection);

    bool del_node_signal_recv = false;
    QObject::connect(&G, &DSRGraph::del_node_signal, &app,
                 [&](uint64_t from, SignalInfo info) {
                     del_node_signal_recv = true;
                 },
                 Qt::QueuedConnection);

    bool deleted_edge_signal_recv = false;
    QObject::connect(&G, &DSRGraph::deleted_edge_signal, &app,
                 [&](const DSR::Edge & edge) {
                     del_edge_signal_recv = true;
                 },
                 Qt::QueuedConnection);

    bool deleted_node_signal_recv = false;
    QObject::connect(&G, &DSRGraph::deleted_node_signal, &app,
                 [&](const DSR::Node & edge) {
                     del_node_signal_recv = true;
                 },
                 Qt::QueuedConnection);


    auto node = G.get_node(*r);
    REQUIRE(node.has_value());
    node->attrs().erase(node->attrs().begin());
    node->attrs().emplace(new_attribute_());
    G.update_node(std::move(*node));

    QTimer::singleShot(0, [&]() {
        REQUIRE(update_node_signal_recv);
        REQUIRE(update_edge_signal_recv == 0);
        REQUIRE(update_node_attr_signal_recv == 1);
        REQUIRE(update_node_attr_signal_size_recv == 2);
        REQUIRE(update_edge_attr_signal_recv == 0);
        REQUIRE(not del_node_signal_recv);
        REQUIRE(not del_edge_signal_recv);
        REQUIRE(not deleted_node_signal_recv);
        REQUIRE(not deleted_edge_signal_recv);
        app.exit();
    });
    app.exec();
}

TEST_CASE("Insert and edge", "[GRAPH][SIGNALS]") {
    const auto sync_mode = GENERATE(SyncMode::CRDT, SyncMode::LWW);
    CAPTURE(sync_mode_label(sync_mode));
    auto ctx = make_empty_config_file();
    auto id1 = rand() % 1000;
    int argc = 0;
    QTestApplication app(argc, nullptr); // need this to trigger signals
    DSRGraph G(make_test_graph_settings(random_string(10), id1, ctx, true, 0, SignalMode::QT, sync_mode));
    auto node_name = random_string();
    auto n = Node::create<testtype_node_type>(node_name);
    const std::optional<uint64_t> r  = G.insert_node(n);
    REQUIRE(r.has_value());

    node_name = random_string();
    n = Node::create<testtype_node_type>(node_name);
    const std::optional<uint64_t> r2  = G.insert_node(n);
    REQUIRE(r2.has_value());

    bool update_node_signal_recv = false;
    QObject::connect(&G, &DSRGraph::update_node_signal, &app,
                     [&](uint64_t, const std::string &type, SignalInfo info) {
                         update_node_signal_recv = true;
                     },
                     Qt::QueuedConnection);


    bool update_edge_signal_recv = false;
    QObject::connect(&G, &DSRGraph::update_edge_signal, &app,
                     [&](uint64_t from, uint64_t to, const std::string &type, SignalInfo info) {
                         update_edge_signal_recv = true;
                     },
                     Qt::QueuedConnection);

    bool update_node_attr_signal_recv = false;
    QObject::connect(&G, &DSRGraph::update_node_attr_signal, &app,
                 [&](uint64_t id ,const std::vector<std::string>& att_names, SignalInfo info) {
                     update_node_attr_signal_recv = true;
                 },
                 Qt::QueuedConnection);

    bool update_edge_attr_signal_recv = false;
    QObject::connect(&G, &DSRGraph::update_edge_attr_signal, &app,
                 [&](uint64_t from, uint64_t to, const std::string &type, const std::vector<std::string>& att_name, SignalInfo info) {
                     update_edge_attr_signal_recv = true;
                 },
                 Qt::QueuedConnection);

    bool del_edge_signal_recv = false;
    QObject::connect(&G, &DSRGraph::del_edge_signal, &app,
                 [&](uint64_t from, uint64_t to, const std::string &edge_tag, SignalInfo info) {
                     del_edge_signal_recv = true;
                 },
                 Qt::QueuedConnection);

    bool del_node_signal_recv = false;
    QObject::connect(&G, &DSRGraph::del_node_signal, &app,
                 [&](uint64_t from, SignalInfo info) {
                     del_node_signal_recv = true;
                 },
                 Qt::QueuedConnection);

    bool deleted_edge_signal_recv = false;
    QObject::connect(&G, &DSRGraph::deleted_edge_signal, &app,
                 [&](const DSR::Edge & edge) {
                     del_edge_signal_recv = true;
                 },
                 Qt::QueuedConnection);

    bool deleted_node_signal_recv = false;
    QObject::connect(&G, &DSRGraph::deleted_node_signal, &app,
                 [&](const DSR::Node & edge) {
                     del_node_signal_recv = true;
                 },
                 Qt::QueuedConnection);

    Edge e = new_edge_(0).second;
    e.from(*r);
    e.to(*r2);
    REQUIRE(G.insert_or_assign_edge(e));

    QTimer::singleShot(0, [&]() {
        REQUIRE(not update_node_signal_recv);
        REQUIRE(update_edge_signal_recv);
        REQUIRE(not update_node_attr_signal_recv);
        REQUIRE(not update_edge_attr_signal_recv);
        REQUIRE(not del_node_signal_recv);
        REQUIRE(not del_edge_signal_recv);
        REQUIRE(not deleted_node_signal_recv);
        REQUIRE(not deleted_edge_signal_recv);
        QTestApplication::exit();
    });
    QTestApplication::exec();
}

TEST_CASE("Update an edge, add and remove attributes", "[GRAPH][SIGNALS]") {
    const auto sync_mode = GENERATE(SyncMode::CRDT, SyncMode::LWW);
    CAPTURE(sync_mode_label(sync_mode));
    auto ctx = make_empty_config_file();
    auto id1 = rand() % 1000;
    int argc = 0;
    QTestApplication app(argc, nullptr); // need this to trigger signals
    DSRGraph G(make_test_graph_settings(random_string(10), id1, ctx, true, 0, SignalMode::QT, sync_mode));
    auto node_name = random_string();
    auto n = Node::create<testtype_node_type>(node_name);
    const std::optional<uint64_t> r  = G.insert_node(n);
    REQUIRE(r.has_value());

    node_name = random_string();
    n = Node::create<testtype_node_type>(node_name);
    const std::optional<uint64_t> r2  = G.insert_node(n);
    REQUIRE(r2.has_value());

    Edge e = new_edge_(0).second;
    e.from(*r);
    e.to(*r2);
    e.attrs({new_attribute_(), new_attribute_(), new_attribute_()});

    G.insert_or_assign_edge(e);

    bool update_node_signal_recv = false;
    QObject::connect(&G, &DSRGraph::update_node_signal, &app,
                     [&](uint64_t, const std::string &type, SignalInfo info) {
                         update_node_signal_recv = true;
                     },
                     Qt::QueuedConnection);


    int update_edge_signal_recv = 0;
    QObject::connect(&G, &DSRGraph::update_edge_signal, &app,
                     [&](uint64_t from, uint64_t to, const std::string &type, SignalInfo info) {
                         update_edge_signal_recv++;
                     },
                     Qt::QueuedConnection);

    int update_node_attr_signal_recv = 0;
    auto update_node_attr_signal_size_recv = 0;
    QObject::connect(&G, &DSRGraph::update_node_attr_signal, &app,
                 [&](uint64_t id ,const std::vector<std::string>& att_names, SignalInfo info) {
                     update_node_attr_signal_recv++;
                     update_node_attr_signal_size_recv = att_names.size();
                 },
                 Qt::QueuedConnection);

    int update_edge_attr_signal_recv = 0;
    auto update_edge_attr_signal_size_recv = 0;
    QObject::connect(&G, &DSRGraph::update_edge_attr_signal, &app,
                 [&](uint64_t from, uint64_t to, const std::string &type, const std::vector<std::string>& att_name, SignalInfo info) {
                     update_edge_attr_signal_recv++;
                     update_edge_attr_signal_size_recv = att_name.size();
                 },
                 Qt::QueuedConnection);

    bool del_edge_signal_recv = false;
    QObject::connect(&G, &DSRGraph::del_edge_signal, &app,
                 [&](uint64_t from, uint64_t to, const std::string &edge_tag, SignalInfo info) {
                     del_edge_signal_recv = true;
                 },
                 Qt::QueuedConnection);

    bool del_node_signal_recv = false;
    QObject::connect(&G, &DSRGraph::del_node_signal, &app,
                 [&](uint64_t from, SignalInfo info) {
                     del_node_signal_recv = true;
                 },
                 Qt::QueuedConnection);

    bool deleted_edge_signal_recv = false;
    QObject::connect(&G, &DSRGraph::deleted_edge_signal, &app,
                 [&](const DSR::Edge & edge) {
                     del_edge_signal_recv = true;
                 },
                 Qt::QueuedConnection);

    bool deleted_node_signal_recv = false;
    QObject::connect(&G, &DSRGraph::deleted_node_signal, &app,
                 [&](const DSR::Node & edge) {
                     del_node_signal_recv = true;
                 },
                 Qt::QueuedConnection);


    auto edge = G.get_edge(*r, *r2, e.type());
    REQUIRE(edge.has_value());
    edge->attrs().erase(edge->attrs().begin());
    edge->attrs().emplace(new_attribute_());
    G.insert_or_assign_edge(*edge);

    QTimer::singleShot(0, [&]() {
        REQUIRE(not update_node_signal_recv);
        REQUIRE(update_edge_signal_recv == 1);
        REQUIRE(update_node_attr_signal_recv == 0);
        REQUIRE(update_node_attr_signal_size_recv == 0);
        REQUIRE(update_edge_attr_signal_recv == 1);
        REQUIRE(update_edge_attr_signal_size_recv == 2);
        REQUIRE(not del_node_signal_recv);
        REQUIRE(not del_edge_signal_recv);
        REQUIRE(not deleted_node_signal_recv);
        REQUIRE(not deleted_edge_signal_recv);
        app.exit();
    });
    app.exec();
}

TEST_CASE("delete a node", "[GRAPH][SIGNALS]") {
    const auto sync_mode = GENERATE(SyncMode::CRDT, SyncMode::LWW);
    CAPTURE(sync_mode_label(sync_mode));
    auto ctx = make_empty_config_file();
    auto id1 = rand() % 1000;
    int argc = 0;
    QTestApplication app(argc, nullptr); // need this to trigger signals
    DSRGraph G(make_test_graph_settings(random_string(10), id1, ctx, true, 0, SignalMode::QT, sync_mode));
    auto node_name = random_string();
    auto n = Node::create<testtype_node_type>(node_name);
    const std::optional<uint64_t> r  = G.insert_node(n);
    REQUIRE(r.has_value());

    node_name = random_string();
    n = Node::create<testtype_node_type>(node_name);
    const std::optional<uint64_t> r2  = G.insert_node(n);
    REQUIRE(r2.has_value());

    Edge e = new_edge_(0).second;
    e.from(*r);
    e.to(*r2);
    e.attrs({new_attribute_(), new_attribute_(), new_attribute_()});
    G.insert_or_assign_edge(e);

    Edge e2 = new_edge_(0).second;
    e2.from(*r2);
    e2.to(*r);
    G.insert_or_assign_edge(e2);

    bool update_node_signal_recv = false;
    QObject::connect(&G, &DSRGraph::update_node_signal, &app,
                     [&](uint64_t, const std::string &type, SignalInfo info) {
                         update_node_signal_recv = true;
                     },
                     Qt::QueuedConnection);


    int update_edge_signal_recv = 0;
    QObject::connect(&G, &DSRGraph::update_edge_signal, &app,
                     [&](uint64_t from, uint64_t to, const std::string &type, SignalInfo info) {
                         update_edge_signal_recv++;
                     },
                     Qt::QueuedConnection);

    int update_node_attr_signal_recv = 0;
    auto update_node_attr_signal_size_recv = 0;
    QObject::connect(&G, &DSRGraph::update_node_attr_signal, &app,
                 [&](uint64_t id ,const std::vector<std::string>& att_names, SignalInfo info) {
                     update_node_attr_signal_recv++;
                     update_node_attr_signal_size_recv = att_names.size();
                 },
                 Qt::QueuedConnection);

    int update_edge_attr_signal_recv = 0;
    auto update_edge_attr_signal_size_recv = 0;
    QObject::connect(&G, &DSRGraph::update_edge_attr_signal, &app,
                 [&](uint64_t from, uint64_t to, const std::string &type, const std::vector<std::string>& att_name, SignalInfo info) {
                     update_edge_attr_signal_recv++;
                     update_edge_attr_signal_size_recv = att_name.size();
                 },
                 Qt::QueuedConnection);

    auto del_edge_signal_recv = 0;
    QObject::connect(&G, &DSRGraph::del_edge_signal, &app,
                 [&](uint64_t from, uint64_t to, const std::string &edge_tag, SignalInfo info) {
                     del_edge_signal_recv++;
                 },
                 Qt::QueuedConnection);

    auto del_node_signal_recv = 0;
    QObject::connect(&G, &DSRGraph::del_node_signal, &app,
                 [&](uint64_t from, SignalInfo info) {
                     del_node_signal_recv++;
                 },
                 Qt::QueuedConnection);

    auto deleted_edge_signal_recv = 0;
    QObject::connect(&G, &DSRGraph::deleted_edge_signal, &app,
                 [&](const DSR::Edge & edge) {
                     deleted_edge_signal_recv++;
                 },
                 Qt::QueuedConnection);

    auto deleted_node_signal_recv = 0;
    QObject::connect(&G, &DSRGraph::deleted_node_signal, &app,
                 [&](const DSR::Node & edge) {
                     deleted_node_signal_recv++;
                 },
                 Qt::QueuedConnection);

    G.delete_node(*r);

    QTimer::singleShot(0, [&]() {
        REQUIRE(not update_node_signal_recv);
        REQUIRE(update_edge_signal_recv == 0);
        REQUIRE(update_node_attr_signal_recv == 0);
        REQUIRE(update_node_attr_signal_size_recv == 0);
        REQUIRE(update_edge_attr_signal_recv == 0);
        REQUIRE(update_edge_attr_signal_size_recv == 0);
        REQUIRE(del_node_signal_recv == 1);
        REQUIRE(deleted_node_signal_recv == 1);
        REQUIRE(deleted_edge_signal_recv == 2);
        REQUIRE(del_edge_signal_recv == 2);
        app.exit();
    });
    app.exec();
}

TEST_CASE("delete an edge", "[GRAPH][SIGNALS]") {
    const auto sync_mode = GENERATE(SyncMode::CRDT, SyncMode::LWW);
    CAPTURE(sync_mode_label(sync_mode));
    auto ctx = make_empty_config_file();
    auto id1 = rand() % 1000;
    int argc = 0;
    QTestApplication app(argc, nullptr); // need this to trigger signals
    DSRGraph G(make_test_graph_settings(random_string(10), id1, ctx, true, 0, SignalMode::QT, sync_mode));
    auto node_name = random_string();
    auto n = Node::create<testtype_node_type>(node_name);
    const std::optional<uint64_t> r  = G.insert_node(n);
    REQUIRE(r.has_value());

    node_name = random_string();
    n = Node::create<testtype_node_type>(node_name);
    const std::optional<uint64_t> r2  = G.insert_node(n);
    REQUIRE(r2.has_value());

    Edge e = new_edge_(0).second;
    e.from(*r);
    e.to(*r2);
    e.attrs({new_attribute_(), new_attribute_(), new_attribute_()});
    G.insert_or_assign_edge(e);

    Edge e2 = new_edge_(0).second;
    e.from(*r2);
    e.to(*r);
    G.insert_or_assign_edge(e2);

    bool update_node_signal_recv = false;
    QObject::connect(&G, &DSRGraph::update_node_signal, &app,
                     [&](uint64_t, const std::string &type, SignalInfo info) {
                         update_node_signal_recv = true;
                     },
                     Qt::QueuedConnection);


    int update_edge_signal_recv = 0;
    QObject::connect(&G, &DSRGraph::update_edge_signal, &app,
                     [&](uint64_t from, uint64_t to, const std::string &type, SignalInfo info) {
                         update_edge_signal_recv++;
                     },
                     Qt::QueuedConnection);

    int update_node_attr_signal_recv = 0;
    auto update_node_attr_signal_size_recv = 0;
    QObject::connect(&G, &DSRGraph::update_node_attr_signal, &app,
                 [&](uint64_t id ,const std::vector<std::string>& att_names, SignalInfo info) {
                     update_node_attr_signal_recv++;
                     update_node_attr_signal_size_recv = att_names.size();
                 },
                 Qt::QueuedConnection);

    int update_edge_attr_signal_recv = 0;
    auto update_edge_attr_signal_size_recv = 0;
    QObject::connect(&G, &DSRGraph::update_edge_attr_signal, &app,
                 [&](uint64_t from, uint64_t to, const std::string &type, const std::vector<std::string>& att_name, SignalInfo info) {
                     update_edge_attr_signal_recv++;
                     update_edge_attr_signal_size_recv = att_name.size();
                 },
                 Qt::QueuedConnection);

    auto del_edge_signal_recv = 0;
    QObject::connect(&G, &DSRGraph::del_edge_signal, &app,
                 [&](uint64_t from, uint64_t to, const std::string &edge_tag, SignalInfo info) {
                     del_edge_signal_recv++;
                 },
                 Qt::QueuedConnection);

    auto del_node_signal_recv = 0;
    QObject::connect(&G, &DSRGraph::del_node_signal, &app,
                 [&](uint64_t from, SignalInfo info) {
                     del_node_signal_recv++;
                 },
                 Qt::QueuedConnection);

    auto deleted_edge_signal_recv = 0;
    QObject::connect(&G, &DSRGraph::deleted_edge_signal, &app,
                 [&](const DSR::Edge & edge) {
                     deleted_edge_signal_recv++;
                 },
                 Qt::QueuedConnection);

    auto deleted_node_signal_recv = 0;
    QObject::connect(&G, &DSRGraph::deleted_node_signal, &app,
                 [&](const DSR::Node & edge) {
                     deleted_node_signal_recv++;
                 },
                 Qt::QueuedConnection);

    G.delete_edge(*r, *r2, e.type());

    QTimer::singleShot(0, [&]() {
        REQUIRE(not update_node_signal_recv);
        REQUIRE(update_edge_signal_recv == 0);
        REQUIRE(update_node_attr_signal_recv == 0);
        REQUIRE(update_node_attr_signal_size_recv == 0);
        REQUIRE(update_edge_attr_signal_recv == 0);
        REQUIRE(update_edge_attr_signal_size_recv == 0);
        REQUIRE(del_node_signal_recv == 0);
        REQUIRE(del_edge_signal_recv == 1);
        REQUIRE(deleted_node_signal_recv == 0);
        REQUIRE(deleted_edge_signal_recv == 1);
        app.exit();
    });
    app.exec();
}

TEST_CASE("RT api signals", "[GRAPH][SIGNALS]") {
    auto ctx = make_empty_config_file();
    auto id1 = rand() % 1000;
    int argc = 0;
    QTestApplication app(argc, nullptr); // need this to trigger signals
    DSRGraph G(random_string(10), id1, ctx);
    auto node_name = random_string();
    auto n = Node::create<testtype_node_type>(node_name);
    G.add_attrib_local<level_att>(n, 0);
    const std::optional<uint64_t> r  = G.insert_node(n);
    REQUIRE(r.has_value());

    node_name = random_string();
    n = Node::create<testtype_node_type>(node_name);
    const std::optional<uint64_t> r2  = G.insert_node(n);
    REQUIRE(r2.has_value());


    int update_node_signal_recv = false;
    QObject::connect(&G, &DSRGraph::update_node_signal, &app,
                     [&](uint64_t, const std::string &type, SignalInfo info) {
                         update_node_signal_recv++;
                     },
                     Qt::QueuedConnection);


    int update_edge_signal_recv = 0;
    QObject::connect(&G, &DSRGraph::update_edge_signal, &app,
                     [&](uint64_t from, uint64_t to, const std::string &type, SignalInfo info) {
                         update_edge_signal_recv++;
                     },
                     Qt::QueuedConnection);

    int update_node_attr_signal_recv = 0;
    auto update_node_attr_signal_size_recv = 0;
    QObject::connect(&G, &DSRGraph::update_node_attr_signal, &app,
                 [&](uint64_t id ,const std::vector<std::string>& att_names, SignalInfo info) {
                     update_node_attr_signal_recv++;
                     update_node_attr_signal_size_recv = att_names.size();
                 },
                 Qt::QueuedConnection);

    int update_edge_attr_signal_recv = 0;
    auto update_edge_attr_signal_size_recv = 0;
    QObject::connect(&G, &DSRGraph::update_edge_attr_signal, &app,
                 [&](uint64_t from, uint64_t to, const std::string &type, const std::vector<std::string>& att_name, SignalInfo info) {
                     update_edge_attr_signal_recv++;
                     update_edge_attr_signal_size_recv = att_name.size();
                 },
                 Qt::QueuedConnection);

    auto del_edge_signal_recv = 0;
    QObject::connect(&G, &DSRGraph::del_edge_signal, &app,
                 [&](uint64_t from, uint64_t to, const std::string &edge_tag, SignalInfo info) {
                     del_edge_signal_recv++;
                 },
                 Qt::QueuedConnection);

    auto del_node_signal_recv = 0;
    QObject::connect(&G, &DSRGraph::del_node_signal, &app,
                 [&](uint64_t from, SignalInfo info) {
                     del_node_signal_recv++;
                 },
                 Qt::QueuedConnection);

    auto deleted_edge_signal_recv = 0;
    QObject::connect(&G, &DSRGraph::deleted_edge_signal, &app,
                 [&](const DSR::Edge & edge) {
                     del_edge_signal_recv++;
                 },
                 Qt::QueuedConnection);

    auto deleted_node_signal_recv = 0;
    QObject::connect(&G, &DSRGraph::deleted_node_signal, &app,
                 [&](const DSR::Node & edge) {
                     del_node_signal_recv++;
                 },
                 Qt::QueuedConnection);


    auto rt = G.get_rt_api();
    REQUIRE (rt);
    auto node = G.get_node(*r);
    REQUIRE(node.has_value());
    n = node.value();
    rt->insert_or_assign_edge_RT(n, *r2, std::vector<float>{0., 0.2, 0.5}, std::vector<float>{0., 0., 0.});

    QTimer::singleShot(0, [&]() {
        REQUIRE(update_node_signal_recv == 1);
        REQUIRE(update_edge_signal_recv == 1);
        REQUIRE(update_node_attr_signal_recv == 1);
        REQUIRE(update_node_attr_signal_size_recv == 2);
        REQUIRE(update_edge_attr_signal_recv == 1);
        REQUIRE(update_edge_attr_signal_size_recv == 2);
        REQUIRE(del_node_signal_recv == 0);
        REQUIRE(del_edge_signal_recv == 0);
        REQUIRE(deleted_node_signal_recv == 0);
        REQUIRE(deleted_edge_signal_recv == 0);
        app.exit();
    });
    app.exec();
}


TEST_CASE("Graph synchronization signals", "[GRAPH][SIGNALS]") {
    const auto sync_mode = GENERATE(SyncMode::CRDT, SyncMode::LWW);
    CAPTURE(sync_mode_label(sync_mode));
}
