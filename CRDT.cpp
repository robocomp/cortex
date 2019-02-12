//
// Created by crivac on 5/02/19.
//
#include "CRDT.h"

using namespace CRDT;

/*
 * Constructor
 */
CRDTGraph::CRDTGraph(int root, std::string name, std::shared_ptr<DSR::Graph> graph_) : graph_root(root), agent_name(name) {
    privateCRDTGraph();
    createIceGraphFromDSRGraph(graph_);
}

CRDTGraph::CRDTGraph(int root, std::string name) : graph_root(root), agent_name(name) {
    privateCRDTGraph();
}

void CRDTGraph::privateCRDTGraph() {
    nodes = Nodes(graph_root);
    filter = "^(?!" + agent_name + "$).*$";

    int argc = 0;
    char *argv[0];
    node = DataStorm::Node(argc, argv);
    work = true;

    // General topic update
    topic = std::make_shared < DataStorm::Topic < std::string, RoboCompDSR::AworSet >> (node, "DSR");
    topic->setWriterDefaultConfig({Ice::nullopt, Ice::nullopt, DataStorm::ClearHistoryPolicy::OnAll});

    // No filter for this topic
    writer = std::make_shared < DataStorm::SingleKeyWriter < std::string, RoboCompDSR::AworSet
            >> (*topic.get(), agent_name);
}

/*
 * Destructor
 */
CRDTGraph::~CRDTGraph() {
    node.shutdown();
    writer.reset();
    topic.reset();
}

/*
 * Add or assign node by id and type
 */
void CRDTGraph::insert_or_assign(int id, const std::string &type_) {
    N new_node;
    new_node.type = type_;
    new_node.id = id;
    auto delta = nodes[id].add(new_node);
    writer->update(translateAwCRDTtoICE(id, delta));
}

/*
 * Add node by content and id
 *
 */
void CRDTGraph::insert_or_assign(int id, const N &node) {
    try {
        if( !(in(id)) ||  get(id)!= node) {
            auto delta = nodes[id].add(node, id);
            writer->update(translateAwCRDTtoICE(id, delta));
        }
    } catch (const std::exception &e) { std::cout << e.what() << " Exception name" << std::endl; };
}

bool CRDTGraph::in(const int &id) {
    nodes.in(id);
}
/*
 * Add new edge to Node
 */
void CRDTGraph::add_edge(int from, int to, const std::string &label_) {
    auto n = get(from);
    n.fano.insert(std::pair(to, RoboCompDSR::EdgeAttribs{label_, from, to, RoboCompDSR::Attribs()}));
    nodes[from].add(n);
}

/*
 * Get node
 *
 */
N CRDTGraph::get(int id) {
    try {
        return nodes[id].readAsList().back();
    }
    catch (const std::exception &e){ std::cout << e.what() << " Exception name" << std::endl;};
}

Nodes CRDTGraph::get() {
    return nodes;
}

/*
 *  Join full graph (for initial sync)
 */
void CRDTGraph::join_full_graph(RoboCompDSR::OrMap full_graph) {
    // Context
    dotcontext<int> dotcontext_aux;
    auto m = static_cast<std::map<int, int>>(full_graph.cbase.cc);
    std::set <pair<int, int>> s;
    for (auto &v : full_graph.cbase.dc)
        s.insert(std::make_pair(v.first, v.second));
    dotcontext_aux.setContext(m, s);

    //Map
    //TODO: Improve. It is not the most efficient.
    for (auto &v : full_graph.m)
        for (auto &awv : translateAwICEtoCRDT(v.first, v.second).readAsListWithId())
            nodes[v.first].add(awv.second, v.first);
}

/*
 * Join delta
 */
void CRDTGraph::join_delta_node(RoboCompDSR::AworSet aworSet) {
    nodes[aworSet.id].join(translateAwICEtoCRDT(aworSet.id, aworSet));
}

/*
 * Clean node and add
 */
void CRDTGraph::replace_node(int id, const N &node) {
    nodes.erase(id);
    insert_or_assign(id, node);
}

/*
 * Add node attribs
 *
 */
void CRDTGraph::add_node_attribs(int id, const RoboCompDSR::Attribs &att) {
    auto n = get(id);
    for (auto &[k, v] : att)
        n.attrs.insert_or_assign(k, v);
    insert_or_assign(id, n);
};

void
CRDTGraph::add_edge_attribs(int from, int to, const RoboCompDSR::Attribs &att)  //HAY QUE METER EL TAG para desambiguar
{
    try {
        auto node = get(from);
        auto &edgeAtts = node.fano.at(to);
        for (auto &[k, v] : att)
            edgeAtts.attrs.insert_or_assign(k, v);
        insert_or_assign(from, node);
    }
    catch(const std::exception &e){ std::cout <<__FILE__ << " " << __FUNCTION__ << " "<< e.what() << std::endl;};
}

RoboCompDSR::AttribValue CRDTGraph::getNodeAttribByName(int id, const std::string &key) {
    try {
        return get(id).attrs.at(key);
    }
    catch(const std::exception &e){
        return RoboCompDSR::AttribValue{"unknown", "unknown", 0};
    };
};


void CRDTGraph::start_subscription_thread(bool showReceived) {
    read_thread = std::thread(&CRDTGraph::subscription_thread, this, showReceived);

}

void CRDTGraph::subscription_thread(bool showReceived) {
    DataStorm::Topic <std::string, RoboCompDSR::AworSet> topic(node, "DSR");
    topic.setReaderDefaultConfig({Ice::nullopt, Ice::nullopt, DataStorm::ClearHistoryPolicy::OnAllExceptPartialUpdate});
    auto reader = DataStorm::FilteredKeyReader<std::string, RoboCompDSR::AworSet>(topic, DataStorm::Filter<std::string>(
            "_regex", filter.c_str())); // Reader excluded self agent
    std::cout << "Starting reader" << std::endl;
    reader.waitForWriters();
    while (true) {
        if (work)
            try {
                auto sample = reader.getNextUnread(); // Get sample
                auto id = sample.getValue().id;
                if (showReceived)
                    std::cout << "Received: node " << sample.getValue() << " from " << sample.getKey() << std::endl;
                join_delta_node(sample.getValue());
                emit updateNodeSIGNAL(id, get(id).type);
            }
            catch (const std::exception &ex) { cerr << ex.what() << endl; }
        usleep(5);
    }
}


void CRDTGraph::start_fullgraph_server_thread() {

    server_thread = std::thread(&CRDTGraph::fullgraph_server_thread, this);
}

void CRDTGraph::fullgraph_server_thread() {
    std::cout << __FUNCTION__ << "->Entering thread to attend full graph requests" << std::endl;
    // create topic and filtered reader for new graph requests
    DataStorm::Topic <std::string, RoboCompDSR::GraphRequest> topic_graph_request(node, "DSR_GRAPH_REQUEST");
    DataStorm::FilteredKeyReader <std::string, RoboCompDSR::GraphRequest> new_graph_reader(topic_graph_request,
                                                                                           DataStorm::Filter<std::string>(
                                                                                                   "_regex",
                                                                                                   filter.c_str()));
    topic_graph_request.setWriterDefaultConfig({Ice::nullopt, Ice::nullopt, DataStorm::ClearHistoryPolicy::Never});
    topic_graph_request.setReaderDefaultConfig({Ice::nullopt, Ice::nullopt, DataStorm::ClearHistoryPolicy::Never});
    auto processSample = [this](auto sample) {
        if (work) {
            work = false;
            std::cout << sample.getValue().from << " asked for full graph" << std::endl;
            DataStorm::Topic <std::string, RoboCompDSR::OrMap> topic_answer(node, "DSR_GRAPH_ANSWER");
            DataStorm::SingleKeyWriter <std::string, RoboCompDSR::OrMap> writer(topic_answer, agent_name,
                                                                                agent_name + " Full Graph Answer");

            topic_answer.setWriterDefaultConfig(
                    {Ice::nullopt, Ice::nullopt, DataStorm::ClearHistoryPolicy::OnAllExceptPartialUpdate});
            topic_answer.setReaderDefaultConfig(
                    {Ice::nullopt, Ice::nullopt, DataStorm::ClearHistoryPolicy::OnAllExceptPartialUpdate});
            writer.add(RoboCompDSR::OrMap{id(), map(), context()});
            std::cout << "Full graph written from lambda" << std::endl;
            sleep(5);
            work = true;
        }
    };
    new_graph_reader.onSamples([processSample](const auto &samples) { for (const auto &s : samples) processSample(s); },
                               processSample);
    node.waitForShutdown();
}

void CRDTGraph::start_fullgraph_request_thread() {
    request_thread = std::thread(&CRDTGraph::fullgraph_request_thread, this);
}


void CRDTGraph::fullgraph_request_thread() {
    std::cout << __FUNCTION__ << ":" << __LINE__ << "-> Initiating request for full graph requests" << std::endl;
    std::chrono::time_point <std::chrono::system_clock> start_clock = std::chrono::system_clock::now();

    DataStorm::Topic <std::string, RoboCompDSR::OrMap> topic_answer(node, "DSR_GRAPH_ANSWER");
    DataStorm::FilteredKeyReader <std::string, RoboCompDSR::OrMap> reader(topic_answer,
                                                                          DataStorm::Filter<std::string>("_regex",
                                                                                                         filter.c_str()));
    topic_answer.setWriterDefaultConfig(
            {Ice::nullopt, Ice::nullopt, DataStorm::ClearHistoryPolicy::OnAllExceptPartialUpdate});
    topic_answer.setReaderDefaultConfig(
            {Ice::nullopt, Ice::nullopt, DataStorm::ClearHistoryPolicy::OnAllExceptPartialUpdate});

    DataStorm::Topic <std::string, RoboCompDSR::GraphRequest> topicR(node, "DSR_GRAPH_REQUEST");
    DataStorm::SingleKeyWriter <std::string, RoboCompDSR::GraphRequest> writer(topicR, agent_name,
                                                                               agent_name + " Full Graph Request");

    topicR.setWriterDefaultConfig({Ice::nullopt, Ice::nullopt, DataStorm::ClearHistoryPolicy::Never});
    topicR.setReaderDefaultConfig({Ice::nullopt, Ice::nullopt, DataStorm::ClearHistoryPolicy::Never});
    writer.add(RoboCompDSR::GraphRequest{agent_name});

    std::cout << __FUNCTION__ << " Wait for writers: " << reader.hasWriters() << ", " << reader.hasUnread()
              << std::endl;
    auto full_graph = reader.getNextUnread();
    if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - start_clock).count() <
        TIMEOUT) {
        join_full_graph(full_graph.getValue());
        std::cout << __FUNCTION__ << " Finished uploading full graph" << std::endl;
    }
}

RoboCompDSR::AworSet CRDTGraph::translateAwCRDTtoICE(int id, aworset<N, int> &data) {
    RoboCompDSR::AworSet delta_crdt;
    for (auto &kv_dots : data.dots().ds)
        delta_crdt.dk.ds[RoboCompDSR::PairInt{kv_dots.first.first, kv_dots.first.second}] = kv_dots.second;
    for (auto &kv_cc : data.context().getCcDc().first)
        delta_crdt.dk.cbase.cc[kv_cc.first] = kv_cc.second;
    for (auto &kv_dc : data.context().getCcDc().second)
        delta_crdt.dk.cbase.dc.push_back(RoboCompDSR::PairInt{kv_dc.first, kv_dc.second});
    delta_crdt.id = id; //TODO: Check K value of aworset (ID=0)
    return delta_crdt;
}

aworset<N, int> CRDTGraph::translateAwICEtoCRDT(int id, RoboCompDSR::AworSet &data) {
    // Context
    dotcontext<int> dotcontext_aux;
    auto m = static_cast<std::map<int, int>>(data.dk.cbase.cc);
    std::set <pair<int, int>> s;
    for (auto &v : data.dk.cbase.dc)
        s.insert(std::make_pair(v.first, v.second));
    dotcontext_aux.setContext(m, s);

    // Dots
    std::map <pair<int, int>, N> ds_aux;
    for (auto &v : data.dk.ds)
        ds_aux[pair<int, int>(v.first.first, v.first.second)] = v.second;

    // Join
    aworset<N, int> aw = aworset<N, int>(id);
    aw.setContext(dotcontext_aux);
    aw.dots().set(ds_aux);
    return aw;
}

void CRDTGraph::clear() {
    nodes.reset();
}

void CRDTGraph::print() {
    std::cout << "----------------------------------------\n" << nodes
              << "----------------------------------------" << std::endl;
}
void CRDTGraph::print(int id) {
    std::cout << "----------------------------------------\n" << nodes[id]
              << "\n----------------------------------------" << std::endl;
}

int CRDTGraph::id() {
    return nodes.getId();
}

RoboCompDSR::MapAworSet CRDTGraph::map() {
    RoboCompDSR::MapAworSet m;
    for (auto &kv : nodes.getMap())  // Map of Aworset to ICE
        m[kv.first] = translateAwCRDTtoICE(kv.first, kv.second);
    return m;
}

RoboCompDSR::DotContext CRDTGraph::context() { // Context to ICE
    RoboCompDSR::DotContext om_dotcontext;
    for (auto &kv_cc : nodes.context().getCcDc().first)
        om_dotcontext.cc[kv_cc.first] = kv_cc.second;
    for (auto &kv_dc : nodes.context().getCcDc().second)
        om_dotcontext.dc.push_back(RoboCompDSR::PairInt{kv_dc.first, kv_dc.second});
    return om_dotcontext;
}


void CRDTGraph::createIceGraphFromDSRGraph(std::shared_ptr<DSR::Graph> graph)
{
    for( const auto &[node_key, node_content] : *graph)
    {
        RoboCompDSR::Attribs node_attrs;
        for(const auto &[attr_key, attr_content] : node_content.attrs) {
            auto attrContent = graph->printVisitorWithType(attr_content);
            node_attrs.insert_or_assign(attr_key, RoboCompDSR::AttribValue{attrContent.first, attrContent.second});
        }
        RoboCompDSR::FanOut node_fano;
        for(const auto &[to, edge_content] : node_content.fanout)
        {
            RoboCompDSR::EdgeAttribs eattrs;
            eattrs.label = edge_content.label;
            eattrs.from = node_key;
            eattrs.to = to;
            for(const auto &[edge_attr_key, edge_attr_content] : edge_content.attrs) {
                auto attrContent = graph->printVisitorWithType(edge_attr_content);
                eattrs.attrs.insert_or_assign(edge_attr_key, RoboCompDSR::AttribValue{attrContent.first, attrContent.second});
            }
            node_fano.insert_or_assign(to, eattrs);
        }
        insert_or_assign(node_key, RoboCompDSR::Node{node_content.type, node_key, node_attrs, node_fano});
    }
}