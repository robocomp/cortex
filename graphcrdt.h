#ifndef GRAPHCRDT_H
#define GRAPHCRDT_H

#include <memory>
#include <chrono>
#include <thread>
#include "graph.h"
#include "src/DSRGraph.h"
#include <DataStorm/DataStorm.h>

namespace DSR
{
    using G = RoboCompDSR::DSRGraph;

    class GraphCRDT : public QObject
    {
        Q_OBJECT
        public:
            GraphCRDT(std::shared_ptr<DSR::Graph> graph_, const std::string &agent_name_);
            ~GraphCRDT(){};
            
        private:
            std::shared_ptr<DSR::Graph> graph;
            DataStorm::Node node;
            std::string agent_name;
            std::shared_ptr<DataStorm::SingleKeyWriter<std::string, G>> writer;
	        std::shared_ptr<DataStorm::Topic<std::string, G>> topic;
	        void subscribeThread();
	        std::thread read_thread;
            G ice_graph;
    
        public slots:
            void NodeAttrsChangedSLOT(const DSR::IDType&, const DSR::Attribs&);
            void addNodeSLOT(std::int32_t id, const std::string &name, const std::string &type, float , float , const std::string&);
            void addEdgeSLOT(std::int32_t from, std::int32_t to, const std::string&);

        signals:

    };
};
#endif // GRAPHCRDT_H
