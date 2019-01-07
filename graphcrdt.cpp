#include "graphcrdt.h"
#include <cppitertools/range.hpp>
#include <qmat/QMatAll>
#include <QApplication>


using namespace DSR;

GraphCRDT::GraphCRDT(std::shared_ptr<DSR::Graph> graph_, const std::string &agent_name_): graph(graph_), agent_name(agent_name_)
{
    qRegisterMetaType<std::int32_t>("std::int32_t");
	qRegisterMetaType<std::string>("std::string");
	qRegisterMetaType<DSR::Attribs>("DSR::Attribs");

    // create DataStorm writer
    int argc = 0; char *argv[0];
    node = DataStorm::Node(argc, argv);
    topic = std::make_shared<DataStorm::Topic<std::string, G>>(node, "DSR");
    topic->setWriterDefaultConfig({ Ice::nullopt, Ice::nullopt, DataStorm::ClearHistoryPolicy::OnAllExceptPartialUpdate });
    // create partial updaters
    topic->setUpdater<RoboCompDSR::Content>("node", 
                    [this](G &g, const RoboCompDSR::Content &node){ g[node.id] = node; this->id_updating_node = node.id;});
    topic->setUpdater<RoboCompDSR::EdgeAttribs>("edge_attribs", 
                    [this](G &g, const RoboCompDSR::EdgeAttribs &eattrs){ 
                                                        try
                                                        {
                                                            g[eattrs.from].fano[eattrs.to] = eattrs; 
                                                            this->id_updating_node = eattrs.from;
                                                            this->id_updating_edge = eattrs.to;
                                                        }
                                                        catch(const std::exception &e){ std::cout << e.what() << std::endl;}
                                                        });
    
    writer = std::make_shared<DataStorm::SingleKeyWriter<std::string, G>>(*topic.get(), agent_name, agent_name + " Writer");

    // we create Ice_graph from graph
    this->createIceGraphFromDSRGraph();
}

GraphCRDT::~GraphCRDT()
{
    node.shutdown();
}

void GraphCRDT::startSubscriptionThread()
{
    read_thread = std::thread(&GraphCRDT::subscribeThread, this);
}

void GraphCRDT::startGraphRequestThread()
{
    serve_full_graph = std::thread(&GraphCRDT::serveFullGraphThread, this);
}

void GraphCRDT::createIceGraphFromDSRGraph()
{
	std::cout << __FUNCTION__ << "-- Entering GraphCRDT::createGraph" << std::endl;
    ice_graph.clear();
    for( const auto &[node_key, node_content] : *graph)
    {
        RoboCompDSR::Attribs node_attrs;
        for(const auto &[attr_key, attr_content] : node_content.attrs)
            node_attrs.insert_or_assign(attr_key, graph->printVisitor(attr_content));
        RoboCompDSR::FanOut node_fano;
        for(const auto &[to, edge_content] : node_content.fanout)
        {
            RoboCompDSR::EdgeAttribs eattrs;
            eattrs.label = edge_content.label;
            eattrs.from = node_key;
            eattrs.to = to;          
            for(const auto &[edge_attr_key, edge_attr_content] : edge_content.attrs)
                eattrs.attrs.insert_or_assign(edge_attr_key, graph->printVisitor(edge_attr_content));
            node_fano.insert_or_assign(to, eattrs);
        }
        ice_graph.insert(std::make_pair(node_key, RoboCompDSR::Content{node_content.type, node_key, node_attrs, node_fano}));
    }
}

///////////////////////////////////////////////////////////////////////////////////
/// Subsription threads
///////////////////////////////////////////////////////////////////////////////////

void GraphCRDT::subscribeThread()
{
    std::cout  << __FILE__ << " " << __FUNCTION__ << " Entering thread to attend graph changes" << std::endl;
    // topic for graph changes distribution
    DataStorm::Topic<std::string, G> topic(node, "DSR");
    topic.setReaderDefaultConfig({ Ice::nullopt, Ice::nullopt, DataStorm::ClearHistoryPolicy::OnAllExceptPartialUpdate });
    topic.setUpdater<RoboCompDSR::Content>("node", 
                    [this](G &g, const RoboCompDSR::Content &node) { g[node.id] = node; this->id_updating_node = node.id ;});
    topic.setUpdater<RoboCompDSR::EdgeAttribs>("edge_attribs", 
                    [this](G &g, const RoboCompDSR::EdgeAttribs &eattrs){ 
                                            try
                                            {
                                                g[eattrs.from].fano[eattrs.to] = eattrs; 
                                                this->id_updating_node = eattrs.from;
                                                this->id_updating_edge = eattrs.to;
                                            }
                                            catch(const std::exception &e){ std::cout << e.what() << std::endl;}});

    // regex to filter out myself as publisher. Filters must be declared in the writer and in the reader
    std::string f = "^(?!" + agent_name + "$).*$";
    
    // create filtered reader for graph changes
    auto reader = std::make_shared<DataStorm::FilteredKeyReader<std::string, G>>(topic, DataStorm::Filter<std::string>("_regex", f.c_str()));
    // create filtered reader for new graph requests
    auto new_connection_reader = std::make_shared<DataStorm::FilteredKeyReader<std::string, G>>(topic, DataStorm::Filter<std::string>("_regex", f.c_str()));

    std::cout << __FILE__ << " " << __FUNCTION__ << " Waiting for a writer to connect..." << std::endl;
    reader->waitForWriters();
   
    auto processSample = [this, reader](auto sample)
                        { 
                            // std::cout << "Sample: " << sample.getKey() << std::endl;
                            if(sample.getEvent() == DataStorm::SampleEvent::PartialUpdate)
                            { 
                                if(sample.getUpdateTag() == "node")
                                    this->updateDSRNode(sample.getValue(), this->id_updating_node); 
                                else if(sample.getUpdateTag() == "edge_attribs")
                                    this->updateDSREdgeAttribs(sample.getValue(), this->id_updating_node, this->id_updating_edge);       
                            }
                            else if(sample.getEvent() == DataStorm::SampleEvent::Add || sample.getEvent() == DataStorm::SampleEvent::Update)
                            { this->copyIceGraphToDSRGraph(sample.getValue()); }
                        };
    
    reader->onSamples([processSample](const auto &samples){ for(const auto &s : samples) processSample(s);}, processSample);
    
    std::cout << __FILE__ << " " << __FUNCTION__ << " SUBSCRIPTION thread running..." << std::endl;
   
    node.waitForShutdown();
}

void GraphCRDT::serveFullGraphThread()
{
    std::cout  << __FILE__ << " " << __FUNCTION__ << " Entering thread to attend full graph requests" << std::endl;
    // regex to filter out myself as publisher. Filters must be declared in the writer and in the reader
    std::string f = "^(?!" + agent_name + "$).*$";
    // create topic and filtered reader for new graph requests
    DataStorm::Topic<std::string, RoboCompDSR::GraphRequest> topic_graph_request(node, "DSR_GRAPH_REQUEST");
    //topic_graph_request.setReaderDefaultConfig({ Ice::nullopt, Ice::nullopt, DataStorm::ClearHistoryPolicy::OnAllExceptPartialUpdate });
    DataStorm::FilteredKeyReader<std::string, RoboCompDSR::GraphRequest> new_graph_reader(topic_graph_request, DataStorm::Filter<std::string>("_regex", f.c_str()));
    
    auto processSample = [this](auto sample)
                         {  std::cout << sample.getValue().from << " asked for full graph" << std::endl; 
                            DataStorm::Topic<std::string, RoboCompDSR::DSRGraph> topic_answer(node, "DSR_GRAPH_ANSWER");
                            topic_answer.setWriterDefaultConfig({ Ice::nullopt, Ice::nullopt, DataStorm::ClearHistoryPolicy::OnAdd });
                            DataStorm::SingleKeyWriter<std::string, RoboCompDSR::DSRGraph> writer(topic_answer, agent_name, agent_name + " Full Graph Answer");
                            // printIceGraph();
                            writer.add(this->ice_graph);
                            std::cout << "Full graph written from lambda" << std::endl; 
                         };  
    new_graph_reader.onSamples([processSample](const auto &samples){ for(const auto &s : samples) processSample(s);}, processSample);
    node.waitForShutdown();
}

///////////////////////////////////////////////////////////////////////////////////
/// Graph updating methods
///////////////////////////////////////////////////////////////////////////////////

void GraphCRDT::updateDSRNode(const RoboCompDSR::DSRGraph &new_ice_graph, const DSR::IDType &id)
{
    //std::cout << __FILE__ << " " << __FUNCTION__ << " updated node " << id << std::endl;
    DSR::Value value;
    for(const auto &[ka, va] : new_ice_graph.at(id).attrs)
        value.attrs.insert_or_assign(ka, translateIceElementToDSRGraph(ka, va));
    graph->addNodeAttribs(id, value.attrs);
}

void GraphCRDT::updateDSREdgeAttribs(const RoboCompDSR::DSRGraph &new_ice_graph, const DSR::IDType &from, const DSR::IDType &to )
{
    //std::cout << __FILE__ << " " << __FUNCTION__ << " updating DSRGraph edge attributes " << from << " " << to << std::endl;
    DSR::Attribs attribs;
    for(const auto &[ka, va] : new_ice_graph.at(from).fano.at(to).attrs)
        attribs.insert_or_assign(ka, translateIceElementToDSRGraph(ka, va));  
    graph->addEdgeAttribs(from, to, attribs);    
}

void GraphCRDT::copyIceGraphToDSRGraph(const RoboCompDSR::DSRGraph &new_ice_graph)
{
    std::cout << __FILE__ << " " << __FUNCTION__ << " Copying Ice to DSRGraph " << std::endl;
    // recorrer ice_graph llamando a la API de graph para reescribirlo entero. Ojo que no lo borra antes

    graph->clear();
    for( const auto &[node_key, node_content] : new_ice_graph)
    {
        DSR::Attribs node_attrs;
        for(const auto &[attr_key, attr_content] : node_content.attrs)
            node_attrs.insert_or_assign(attr_key, translateIceElementToDSRGraph(attr_key, attr_content));
        DSR::FanOut node_fano;
        for(const auto &[to, edge_content] : node_content.fano)
        {
            DSR::EdgeAttribs eattrs;
            eattrs.label = edge_content.label;
            eattrs.from = node_key;
            eattrs.to = to;          
            for(const auto &[edge_attr_key, edge_attr_content] : edge_content.attrs)
            {
                //std::cout << __FUNCTION__ << " key:" << edge_attr_key << " from " << node_key << " to "  << to << " " << edge_attr_content << std::endl;
                eattrs.attrs.insert_or_assign(edge_attr_key, translateIceElementToDSRGraph(edge_attr_key, edge_attr_content));
                // if(edge_attr_key == "RT") 
                //     std::get<RTMat>(translateIceElementToDSRGraph(edge_attr_key, edge_attr_content)).print("rt");
            }
            node_fano.insert_or_assign(to, eattrs);
        }
        graph->addNode(node_key, DSR::Value{node_content.type, node_key, node_attrs, node_fano});
    }
    std::cout << __FILE__ << " " << __FUNCTION__ << " Finished Copying Ice to DSRGraph " << std::endl;
}

// Converts Ice string values into DSRGraph native types
DSR::MTypes GraphCRDT::translateIceElementToDSRGraph(const std::string &name, const std::string &val)
{
    // WE NEED TO ADD A TYPE field to the Attribute values and get rid of this shit
    static const std::list<std::string> string_types{ "imName", "imType", "tableType", "texture", "engine", "path", "render", "color", "name"};
    static const std::list<std::string> bool_types{ "collidable", "collide"};
    static const std::list<std::string> RT_types{ "RT"};
    static const std::list<std::string> vector_float_types{ "laser_data_dists", "laser_data_angles"};

    DSR::MTypes res;
    if(std::find(string_types.begin(), string_types.end(), name) != string_types.end())
        res = val;
    else if(std::find(bool_types.begin(), bool_types.end(), name) != bool_types.end())
    { if( val == "true") res = true; else res = false; }
    else if(std::find(vector_float_types.begin(), vector_float_types.end(), name) != vector_float_types.end())
    {
        std::vector<float> numbers;
        std::istringstream iss(val);
        std::transform(std::istream_iterator<std::string>(iss), std::istream_iterator<std::string>(), 
                        std::back_inserter(numbers), [](const std::string &s){ return (float)std::stof(s);});
        res = numbers;
    }
    // instantiate a QMat from string marshalling
    else if(std::find(RT_types.begin(), RT_types.end(), name) != RT_types.end())
    {
        std::vector<float> ns;
        std::istringstream iss(val);
        std::transform(std::istream_iterator<std::string>(iss), std::istream_iterator<std::string>(), 
                        std::back_inserter(ns), [](const std::string &s){ return QString::fromStdString(s).toFloat();});
        RMat::RTMat rt;
        // std::cout << "------ in translating ------";
        // for(auto n:ns) std::cout << n << " ";
        // std::cout << std::endl;
        if(ns.size() == 6)
        {
            rt.set(ns[3],ns[4],ns[5],ns[0],ns[1],ns[2]);
            //rt.print("in translate");
        }
        else
        {
            std::cout << __FILE__ << __FUNCTION__ << "Error reading RTMat. Initializing with identity";
            rt = QMat::identity(4);
        }
    	return(rt);
    }
	else 
    {   
        try
        { res = (float)std::stod(val); }
        catch(const std::exception &e) 
        { std::cout << __FUNCTION__ << "catch: " << name << " " << val << std::endl; res = std::string{""}; }
    }
    return res;
};

/// Ask for a full graph using simulated RPC connection  

void GraphCRDT::newGraphRequestAndWait()
{
    // make the request for a full graph
    std::cout << __FILE__ << " " << __FUNCTION__ << " Initiating request for full graph requests" << std::endl;

    // READER: regex to filter out myself as publisher. Filters must be declared in the writer and in the reader
    std::string f = "^(?!" + agent_name + "$).*$";
    // create a topic and a filtered reader for new graph requests
    DataStorm::Topic<std::string, RoboCompDSR::DSRGraph> topic_answer(node, "DSR_GRAPH_ANSWER");
    //topic_answer.setReaderDefaultConfig({ Ice::nullopt, Ice::nullopt, DataStorm::ClearHistoryPolicy::OnAllExceptPartialUpdate });
    DataStorm::FilteredKeyReader<std::string, RoboCompDSR::DSRGraph> reader(topic_answer, DataStorm::Filter<std::string>("_regex", f.c_str()));

     // WRITER:topic for graph request when an agent incorportates
    DataStorm::Topic<std::string, RoboCompDSR::GraphRequest> topicR(node, "DSR_GRAPH_REQUEST");
    //topicR.setWriterDefaultConfig({ Ice::nullopt, Ice::nullopt, DataStorm::ClearHistoryPolicy::Never });
    DataStorm::SingleKeyWriter<std::string, RoboCompDSR::GraphRequest> writer(topicR, agent_name, agent_name + " Full Graph Request");
    RoboCompDSR::GraphRequest request{ agent_name };
    writer.add(request);

    // while(!reader.hasWriters())
    // { 
    //     std::cout << __FILE__ << __FUNCTION__ << " Waiting for a writer to come up..." << std::endl;
    //     std::this_thread::sleep_for(100ms);
    // }
    auto sample = reader.getNextUnread();
    this->ice_graph = sample.getValue();
    copyIceGraphToDSRGraph(this->ice_graph);
    std::cout << __FILE__ << " " << __FUNCTION__ << " Finished uploading full graph" << std::endl;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////
///// Graph changing SLOTS
///////////////////////////////////////////////////////////////////////////////////////////////////////

void GraphCRDT::NodeAttrsChangedSLOT(const std::int32_t id, const DSR::Attribs& attrs)
{
    //std::cout << __FUNCTION__ << " Attribute change in node " << id << std::endl;
    try
    {
        //auto node = writer->getLast().getValue().node;
        auto node = ice_graph.at(id);
        for(const auto &[k,v] : graph->getNodeAttrs(id))
            node.attrs.insert_or_assign(k, graph->printVisitor(v));   //ADD FANOUT
        node.id = id;
        node.type = graph->getNodeType(id);
        if( writer->hasReaders()) 
            writer->partialUpdate<RoboCompDSR::Content>("node")(node);
    }  
    catch(const std::exception &e) { std::cout << e.what() << std::endl;}  
}

void GraphCRDT::EdgeAttrsChangedSLOT(const DSR::IDType &from, const DSR::IDType &to)
{
    //std::cout << __FILE__ << " " << __FUNCTION__ << " Attribute change in edge from " << from << " to " << to << std::endl;
    try
    {
        auto eattrs = ice_graph.at(from).fano.at(to);
        for(const auto &[k,v] : graph->getEdgeAttrs(from,to))
            eattrs.attrs.insert_or_assign(k, graph->printVisitor(v));  
        if( writer->hasReaders()) 
        {
            //std::cout << "before sending " << eattrs.attrs.at("RT") << std::endl;
            writer->partialUpdate<RoboCompDSR::EdgeAttribs>("edge_attribs")(eattrs);
        }
    }  
    catch(const std::exception &e) { std::cout << "Exception at " << __FUNCTION__ << ": " << e.what() << std::endl;}  
}

void GraphCRDT::addNodeSLOT(std::int32_t id, const std::string &type)
{
    ice_graph.insert_or_assign(id, RoboCompDSR::Content{type, id, RoboCompDSR::Attribs(), RoboCompDSR::FanOut()});
}

void GraphCRDT::addEdgeSLOT(std::int32_t from, std::int32_t to, const std::string &tag)
{
    ice_graph.at(from).fano.insert_or_assign(to, RoboCompDSR::EdgeAttribs{tag, from, to, RoboCompDSR::Attribs()});
}

///////////////////////////////////////////////////////////////////////////////////
/// Printing utilities
///////////////////////////////////////////////////////////////////////////////////
void GraphCRDT::printIceGraph()
{
    std::cout << std::endl;
    std::cout << "----------------------------------Ice Graph---------------------------" << std::endl;
    for( const auto &[node_key, node_content] : ice_graph)
    {
        for(const auto &[attr_key, attr_content] : node_content.attrs)
            std::cout << "Node: " << node_key << " Att: " << attr_key << ": " << attr_content << std::endl;
        for(const auto &[to, edge_content] : node_content.fano)
        {
            std::cout << "  Edge from " << node_key << " to " << to << " label " << edge_content.label << std::endl;          
            for(const auto &[edge_attr_key, edge_attr_content] : edge_content.attrs)
                std::cout << "      Edge attr: " << edge_attr_key << ": " << edge_attr_content << std::endl;          
        }
    }
    std::cout << "-------------------------------End Ice Graph---------------------------" << std::endl;
}

//////////////////////////////////
// FUSCA

 // reader.onConnectedWriters([](const auto &ws){for(auto w:ws) std::cout << w << " ";}, 
    //     [](auto reason, auto writer){ std::cout << "WRITER: "  << writer << std::endl;
    //                                   if( reason == DataStorm::CallbackReason::Connect) std::cout << "Connect" << std::endl;
    //                                   else std::cout << "Disconnect" << std::endl;});
    // reader.onConnectedKeys([](auto init){for(auto i:init) std::cout << i << " ";}, 
    //                        [](auto reason, auto key){ std::cout << "KEY: "  << key << std::endl;
    //                                   if( reason == DataStorm::CallbackReason::Connect) std::cout << "Connect" << std::endl;
    //                                   else std::cout << "Disconnect" << std::endl;});