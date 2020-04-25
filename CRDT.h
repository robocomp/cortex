//
// Created by crivac on 17/01/19.
//

#ifndef CRDT_GRAPH
#define CRDT_GRAPH

#include <iostream>
#include <map>
#include <chrono>
#include <thread>

#include <mutex>
#include <shared_mutex>

#include "libs/delta-crdts.cc"


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

// Overload pattern used inprintVisitor
template<class... Ts> struct overload : Ts... { using Ts::operator()...; };
template<class... Ts> overload(Ts...) -> overload<Ts...>;


namespace CRDT
{



using N = Node;
using Nodes = ormap<int, aworset<N,  int >, int>;
using MTypes = std::variant<std::uint32_t, std::int32_t, float, std::string, std::vector<float>, RMat::RTMat>;
using IDType = std::int32_t;
using Attribs = std::unordered_map<std::string, MTypes>;



/////////////////////////////////////////////////////////////////
/// CRDT API
/////////////////////////////////////////////////////////////////

class CRDTGraph : public QObject
{
    Q_OBJECT
public:
    size_t size() const { return nodes.getMap().size(); };
    CRDTGraph(int root, std::string name, int id);
    ~CRDTGraph();

    // threads
    void start_fullgraph_request_thread();
    void start_fullgraph_server_thread();
    void start_subscription_thread(bool showReceived);

    //////////////////////////////////////////////////////
    ///  Graph API
    //////////////////////////////////////////////////////

    // Utils
    void read_from_file(const std::string &xml_file_path);
    bool empty(const int &id);


    // Nodes
    Node get_node(const std::string& name);
    Node get_node(int id);
    bool insert_or_assign_node(const N &node);
    bool delete_node(const std::string &name);
    bool delete_node(int id);

    //Edges
    EdgeAttribs get_edge(const std::string& from, const std::string& to);
    bool insert_or_assign_edge(const EdgeAttribs& attrs);
    bool delete_edge(const std::string& from, const std::string& t);

    std::string get_name_from_id(std::int32_t id);
    int get_id_from_name(const std::string &name);

    //////////////////////////////////////////////////////
    ///  Viewer
    //////////////////////////////////////////////////////
    Nodes get();
    N get(int id);
    
    // gets a const node ref and searches an attrib by name. With a map should be constant time.
    AttribValue get_node_attrib_by_name(const Node& n, const std::string &key);
   

    template <typename Ta>
    Ta get_node_attrib_by_name(Node& n, const std::string &key)
    {
        AttribValue av = get_node_attrib_by_name(n, key);
        return icevalue_to_nativetype<Ta>(key, av.value());
    }
    std::tuple<std::string, std::string, int> mtype_to_icevalue(const MTypes &t);
    template <typename Ta>
    Ta icevalue_to_nativetype(const std::string &name, const std::string &val)
    {
        return std::get<Ta>(icevalue_to_mtypes(name, val));
    };
    MTypes icevalue_to_mtypes(const std::string &name, const std::string &val);
    std::int32_t get_node_level(Node& n);
    std::string get_node_type(Node& n);


    void add_attrib(std::map<string, AttribValue> &v, std::string att_name, CRDT::MTypes att_value);
    //void add_edge_attribs(vector<EdgeAttribs> &v, EdgeAttribs& ea);

    //////////////////////////////////////////////////////


    //For debug
    int count = 0;

private:
    Nodes nodes;
    int graph_root;
    bool work;
    mutable std::shared_mutex _mutex;
    //std::thread read_thread, request_thread, server_thread; // Threads
    std::string filter;
    std::string agent_name;
    const int agent_id;

    std::map<string, int> name_map;     // mapping between name and id of nodes
    std::map<int, string> id_map;

    bool in(const int &id);
    N get_(int id);
    bool insert_or_assign_node_(const N &node);


    int id();
    DotContext context();
    std::map<int, AworSet> Map();
    
    void join_delta_node(AworSet aworSet);
    void join_full_graph(OrMap full_graph);



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

    void update_attrs_signal(const std::int32_t &id, const std::map<string, AttribValue> &attribs); //Signal to show node attribs.
    void update_edge_signal(const std::int32_t from, const std::int32_t to);                   // Signal to show edge attribs.

    void del_edge_signal(const std::int32_t from, const std::int32_t to, const std::string &edge_tag); // Signal to del edge.
    void del_node_signal(const std::int32_t from);                                                     // Signal to del node.
};

} // namespace CRDT

#endif
