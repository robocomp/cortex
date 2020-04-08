//
// Created by crivac on 17/01/19.
//

#ifndef CRDT_GRAPH
#define CRDT_GRAPH

#include <iostream>
#include <map>
#include <chrono>
#include <thread>
#include "graph.h"
//#include "libs/DSRGraph.h"
#include "libs/delta-crdts.cc"
//#include <DataStorm/DataStorm.h>

#include "fast_rtps/dsrparticipant.h"
#include "fast_rtps/dsrpublisher.h"
#include "fast_rtps/dsrsubscriber.h"

#include "topics/DSRGraphPubSubTypes.h"

#include <any>
#include <memory>
#include <vector>
#include <variant>
#include <qmat/QMatAll>
#include <typeinfo>

#define NO_PARENT -1
#define TIMEOUT 5

namespace CRDT
{

using N = Node;
using Nodes = ormap<int, aworset<N, int>, int>;
using MTypes = std::variant<std::uint32_t, std::int32_t, float, std::string, std::vector<float>, RMat::RTMat>;

/////////////////////////////////////////////////////////////////
/// CRDT API
/////////////////////////////////////////////////////////////////

class CRDTGraph : public QObject
{
    Q_OBJECT
public:
    class NewMessageFunctor
    {
    public:
        CRDTGraph *graph;
        bool *work;
        std::function<void(eprosima::fastrtps::Subscriber *sub, bool *work, CRDT::CRDTGraph *graph)> f;

        NewMessageFunctor(CRDTGraph *graph_, bool *work_,
                std::function<void(eprosima::fastrtps::Subscriber *sub, bool *work, CRDT::CRDTGraph *graph)> f_)
            : graph(graph_), work(work_), f(std::move(f_)){}

        NewMessageFunctor() {};


        void operator()(eprosima::fastrtps::Subscriber *sub) { f(sub, work, graph); };
    };

    size_t size() const { return nodes.getMap().size(); };

    CRDTGraph(int root, std::string name);
    ~CRDTGraph();

    //////////////////////////////////////////////////////
    ///  Graph API
    //////////////////////////////////////////////////////

    void insert_or_assign(int id, const std::string &type_);
    void insert_or_assign(int id, const N &node);
    void insert_or_assign(const N &node);

    void add_edge(int from, int to, const std::string &label_);
    void add_edge_attrib(int from, int to, std::string att_name, CRDT::MTypes att_value);
    void add_edge_attrib(int from, int to, std::string att_name, std::string att_type, std::string att_value, int length);
    void add_edge_attribs(int from, int to, const vector<AttribValue> &att);

    void add_node_attrib(int id, std::string att_name, CRDT::MTypes att_value);
    void add_node_attrib(int id, std::string att_name, std::string att_type, std::string att_value, int length);
    void add_node_attribs(int id, const vector<AttribValue> &att);

    void delete_node(int id);
    void delete_node(string name);
    bool empty(const int &id);

    //////////////////////////////////////////////////////////

    std::vector<EdgeAttribs> getEdges(int id);
    Nodes get();
    list<N> get_list();
    N get(int id);
    vector<AttribValue> get_node_attribs_crdt(int id);
    std::map<std::string, MTypes> get_node_attribs(int id);
    AttribValue get_node_attrib_by_name(int id, const std::string &key);

    int get_id_from_name(const std::string &tag);

    template <typename Ta>
    Ta get_node_attrib_by_name(int id, const std::string &key)
    {
        AttribValue av = get_node_attrib_by_name(id, key);
        return icevalue_to_nativetype<Ta>(key, av.value());
    }
    EdgeAttribs get_edge_attrib(int from, int to);

    std::tuple<std::string, std::string, int> mtype_to_icevalue(const MTypes &t);

    // Converts Ice string values into DSRGraph native types
    template <typename Ta>
    Ta icevalue_to_nativetype(const std::string &name, const std::string &val)
    {
        return std::get<Ta>(icevalue_to_mtypes(name, val));
    };
    MTypes icevalue_to_mtypes(const std::string &name, const std::string &val);
    std::int32_t get_node_level(std::int32_t id);
    std::string get_node_type(std::int32_t id);
    std::int32_t get_parent_id(std::int32_t id);

    bool in(const int &id);

    void join_delta_node(AworSet aworSet);
    void join_full_graph(OrMap full_graph);

    void print();
    void print(int id);
    std::string printVisitor(const MTypes &t);

    // Utils
    void read_from_file(const std::string &xml_file_path);
    void replace_node(int id, const N &node);

    // hreads
    void start_fullgraph_request_thread();
    void start_fullgraph_server_thread();
    void start_subscription_thread(bool showReceived);

    //int getId();
    //DotContext getContext();
    //std::map< int, AworSet> getMap();

    std::string agent_name;

private:
    Nodes nodes;
    int graph_root;
    bool work;
    mutable std::mutex _mutex;
    std::thread read_thread, request_thread, server_thread; // Threads

    std::string filter;

    //DataStorm::Node node; // Main datastorm node
    //std::shared_ptr<DataStorm::SingleKeyWriter<std::string, AworSet >> writer;
    //std::shared_ptr<DataStorm::Topic<std::string, AworSet >> topic;

    int id();
    DotContext context();
    std::vector<AworSet> Map();

    void clear();

    // Threads handlers
    void subscription_thread(bool showReceived);
    void fullgraph_server_thread();
    void fullgraph_request_thread();

    // Translators
    AworSet translateAwCRDTtoICE(int id, aworset<N, int> &data);
    aworset<N, int> translateAwICEtoCRDT(AworSet &data);

    // RTSP participant
    DSRParticipant dsrparticipant;
    DSRPublisher dsrpub;
    DSRSubscriber dsrsub;
    NewMessageFunctor dsrpub_call;

    DSRSubscriber dsrsub_graph_request;
    DSRPublisher dsrpub_graph_request;
    NewMessageFunctor dsrpub_graph_request_call;

    DSRSubscriber dsrsub_request_answer;
    DSRPublisher dsrpub_request_answer;
    NewMessageFunctor dsrpub_request_answer_call;

signals:                                                                  // for graphics update
    void update_node_signal(const std::int32_t, const std::string &type); // Signal to update CRDT

    void update_attrs_signal(const std::int32_t &id, const vector<AttribValue> &attribs); //Signal to show node attribs.
    void update_edge_signal(const std::int32_t from, const std::int32_t to);                   // Signal to show edge attribs.

    void del_edge_signal(const std::int32_t from, const std::int32_t to, const std::string &edge_tag); // Signal to del edge.
    void del_node_signal(const std::int32_t from);                                                     // Signal to del node.
};

} // namespace CRDT

#endif
