#ifndef GRAPHCRDT_H
#define GRAPHCRDT_H

#include <memory>
#include <chrono>
#include <thread>
#include <sstream>
#include <iterator>
#include "graph.h"
#include "../../graph-related-classes/DSRGraph.h"
#include <DataStorm/DataStorm.h>

namespace DSR
{
    using G = RoboCompDSR::DSRPackage;
    using namespace std::chrono_literals;  
    
    //////////////////////////////////////////////////////////////////////////////////////////////77
	/// CRDT publishing/subscribing controller to distribute the graph using DatsStorm with UDP multicast
	//////////////////////////////////////////////////////////////////////////////////////////////77

    class GraphCRDT : public QObject
    {
        Q_OBJECT
        public:
            GraphCRDT(std::shared_ptr<DSR::Graph> graph_, const std::string &agent_name_);
            ~GraphCRDT();
            
        private:
            std::shared_ptr<DSR::Graph> graph;
            DataStorm::Node node;
            std::string agent_name;
            std::shared_ptr<DataStorm::SingleKeyWriter<std::string, G>> writer;
	        std::shared_ptr<DataStorm::Topic<std::string, G>> topic;
            std::thread read_thread;
            G ice_package;

	        void subscribeThread();
            void createIceGraph();	 
            void copyIceGraphToDSR(const RoboCompDSR::DSRGraph &new_ice_graph);     
            void updateDSRNode(const RoboCompDSR::DSRGraph &new_ice_graph, const DSR::IDType &id);     
            DSR::MTypes iceToGraph(const std::string &type, const std::string &val);
    
        public slots:
            void NodeAttrsChangedSLOT(const std::int32_t, const DSR::Attribs&);
            void addNodeSLOT(std::int32_t id, const std::string &type);
            void addEdgeSLOT(std::int32_t from, std::int32_t to, const std::string&);

        signals:

    };
};
#endif // GRAPHCRDT_H
