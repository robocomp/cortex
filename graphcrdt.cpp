#include "graphcrdt.h"
#include <cppitertools/range.hpp>
#include <qmat/QMatAll>
#include <QApplication>


using namespace DSR;

GraphCRDT::GraphCRDT(std::shared_ptr<DSR::Graph> graph_, const std::string &agent_name_)
{
    graph = graph_;
    agent_name = agent_name_;
    int argc = 0; char *argv[0];
    node = DataStorm::Node(argc, argv);
    topic = std::make_shared<DataStorm::Topic<std::string, G>>(node, "DSR");
    topic->setWriterDefaultConfig({ Ice::nullopt, Ice::nullopt, DataStorm::ClearHistoryPolicy::Never });
    writer = std::make_shared<DataStorm::SingleKeyWriter<std::string, G>>(*topic.get(), agent_name);

    // init subscription thread
    read_thread = std::thread(&GraphCRDT::subscribeThread, this);

}

void createIceGraph()
{


}

void GraphCRDT::subscribeThread()
{
    DataStorm::Topic<std::string, G> topic(node, "DSR");
    topic.setReaderDefaultConfig({ Ice::nullopt, Ice::nullopt, DataStorm::ClearHistoryPolicy::Never });
    // regex to filter out myself as publisher
    std::string f = "^(?!" + agent_name + "$).*$";
    auto reader = DataStorm::makeFilteredKeyReader(topic, DataStorm::Filter<std::string>("_regex", f.c_str()));
    std::cout << "starting reader " << std::endl;
    reader.waitForWriters();
    while(true)
    {
        try
        {
            auto sample = reader.getNextUnread();
            for( const auto &[k,v] : sample.getValue())
                std::cout << "Received: node " << k << "with laser_data " << v.attrs.at("laser_data_dists") << " from " << sample.getKey() << std::endl;
            std::cout << "--------------------" << std::endl;
        }
        catch (const std::exception &ex) 
        {   std::cout << ex.what() << std::endl;  }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////7
///// SLOTS
///////////////////////////////////////////////////////////////////////////////////////////////////////

void GraphCRDT::NodeAttrsChangedSLOT(const DSR::IDType& id, const DSR::Attribs& attrs)
{
    std::cout << __FUNCTION__ << " Attribute change in node " << id << std::endl;
    // update ice_graph 
    for(const auto &[k,v] : attrs)
     {
        // std::cout << k << ": " << graph->printVisitor(v) << std::endl;
        ice_graph.at(id).attrs.insert_or_assign(k, graph->printVisitor(v));
     }  
    writer->update(ice_graph);
}

void GraphCRDT::addNodeSLOT(std::int32_t id, const std::string &name, const std::string &type, float posx, float posy, const std::string &color_name)
{
    ice_graph.insert(std::make_pair(id, RoboCompDSR::Content{type, id, RoboCompDSR::Attribs(), RoboCompDSR::FanOut()}));
}

void GraphCRDT::addEdgeSLOT(std::int32_t from, std::int32_t to, const std::string &tag)
{
    ice_graph.at(from).fano.insert(std::make_pair(to, RoboCompDSR::EdgeAttribs{tag, from, to, RoboCompDSR::Attribs()}));
}
