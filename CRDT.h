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
//using MTypes = std::variant<std::uint32_t, std::int32_t, float, std::string, std::vector<float>, RMat::RTMat>;
using MTypes = std::variant<std::string, std::int32_t, float , std::vector<float>, RMat::RTMat>;
using IDType = std::int32_t;
using Attribs = std::unordered_map<std::string, MTypes>;



/////////////////////////////////////////////////////////////////
/// CRDT API
/////////////////////////////////////////////////////////////////

class CRDTGraph : public QObject
{
    Q_OBJECT
public:
    size_t size();
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
	void read_from_json_file(const std::string &json_file_path);
    void write_to_json_file(const std::string &json_file_path);
    bool empty(const int &id);


    // Nodes
    Node get_node(const std::string& name);
    Node get_node(int id);
    bool insert_or_assign_node(const N &node);
    bool delete_node(const std::string &name);
    bool delete_node(int id);
    // typename std::map<int, aworset<N,int>>::const_iterator begin() const { return nodes.getMap().begin(); };
	// typename std::map<int, aworset<N,int>>::const_iterator end() const { return nodes.getMap().end(); };

    //Edges
    EdgeAttribs get_edge(const std::string& from, const std::string& to, const std::string& key);
    EdgeAttribs get_edge(int from, int to, const std::string& key);

    bool insert_or_assign_edge(const EdgeAttribs& attrs);
    bool delete_edge(const std::string& from, const std::string& t);
    bool delete_edge(int from, int t);

    std::string get_name_from_id(std::int32_t id);
    int get_id_from_name(const std::string &name);

    //////////////////////////////////////////////////////
    ///  Viewer
    //////////////////////////////////////////////////////
    Nodes get();
    N get(int id);
    
    // gets a const node ref and searches an attrib by name. With a map should be constant time.

    template <typename T, typename = std::enable_if_t<std::is_same<Node,  T>::value || std::is_same<EdgeAttribs, T>::value ,T >  >
    AttribValue get_attrib_by_name_(const T& n, const std::string &key) {
        try {
            auto attrs = n.attrs();
            auto value  = attrs.find(key);

            if (value != attrs.end()) {
                return value->second;
            }

            AttribValue av;
            av.type(STRING);
            Val v;
            v.str("unkown");
            av.value(v);
            av.key(key);

            return av;
        }
        catch(const std::exception &e){
            if constexpr (std::is_same<Node,  T>::value) {
                std::cout << "EXCEPTION: " << __FILE__ << " " << __FUNCTION__ << ":" << __LINE__ << " " << e.what()
                          << "-> " << n.id() << std::endl;
            }
            if constexpr (std::is_same<EdgeAttribs,  T>::value) {
                std::cout << "EXCEPTION: " << __FILE__ << " " << __FUNCTION__ << ":" << __LINE__ << " " << e.what()
                          << "-> " << n.to() << std::endl;
            }
            AttribValue av;
            av.type(STRING);
            Val v;
            v.str("unkown");
            av.value(v);
            av.key(key);

            return av;
        };
    }



    template <typename Ta, typename Type, typename =  std::enable_if_t<std::is_same<Node,  Type>::value || std::is_same<EdgeAttribs, Type>::value, Type>>
    Ta get_attrib_by_name(Type& n, const std::string &key) {
        AttribValue av = get_attrib_by_name_(n, key);
        if constexpr (std::is_same<Ta, std::string>::value) return av.value().str();
        if constexpr (std::is_same<Ta, std::int32_t>::value) return av.value().dec();
        if constexpr (std::is_same<Ta, float>::value) return av.value().fl();
        if constexpr (std::is_same<Ta, std::vector<float>>::value)
        {
            if (key == "RT" || key == "RTMat") return av.value().rtmat();
            return av.value().float_vec();
        }
        if constexpr (std::is_same<Ta, std::array<float, 16>>::value) return av.value().rtmat();
        if constexpr (std::is_same<Ta, RMat::RTMat>::value) {
            return RTMat { QMat{ av.value().rtmat()} } ;
        }

    }



    template <typename Ta>
    Ta string_to_nativetype(const std::string &name, const std::string &val)
    {
        return std::get<Ta>(string_to_mtypes(name, val));
    };

    std::tuple<std::string, std::string, int> nativetype_to_string(const MTypes &t);

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
    std::pair<bool, vector<tuple<int, int, std::string>>> delete_node_(int id);
    bool delete_edge_(int from, int t);
    EdgeAttribs get_edge_(int from, int to, const std::string& key);


    int id();
    DotContext context();
    std::map<int, AworSet> Map();
    
    void join_delta_node(AworSet aworSet);
    void join_full_graph(OrMap full_graph);

    MTypes string_to_mtypes(const std::string &name, const std::string &val);



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
