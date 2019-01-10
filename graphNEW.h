#ifndef GRAPH2_H
#define GRAPH2_H

#include <iostream>
#include <map>
#include <memory>
#include <vector>
#include <variant>
#include <qmat/qrtmat.h>
#include "../../graph-related-classes/DSRGraph.h"

namespace DSR
{
    #define NO_PARENT -1

    // Overload pattern used inprintVisitor
    template<class... Ts> struct overload : Ts... { using Ts::operator()...; };
    template<class... Ts> overload(Ts...) -> overload<Ts...>;

    using G = RoboCompDSR::DSRGraph;
    using namespace std::chrono_literals;  
	using IDType = std::int32_t;
	using IMType = QString;
	using MTypes = std::variant<std::int32_t, float, std::string, std::vector<float>, RMat::RTMat>;		
    using Lock = std::lock_guard<std::recursive_mutex>;

    // class AbstractGraph
    // {
    // }

    // Proxy pattern
    template<typename T>
    class NodePrx : public T
    {
        public:
            NodePrx(T *obj) : prx(obj) { mutex.lock(); }
            ~NodePrx()                 { std::cout << "outside" << std::endl; mutex.unlock(); }
            T* operator->()            { std::cout << "operator" << prx->id << std::endl; return prx;};
			//IDType id() const{ return prx->id();};
        private:
            T *prx;
            mutable std::recursive_mutex mutex;
    };

	class Graph : public QObject
	{
		Q_OBJECT
		public:		 
			////////////////////////////////////////////////////////////////////////////////////////////
			//									Thread safe graph API 
			////////////////////////////////////////////////////////////////////////////////////////////
			///// Node access methods
			////////////////////////////////////////////////////////////////////////////////////////////
            std::unique_ptr<NodePrx<RoboCompDSR::Content>> getNodePtr(IDType id) 
            { 
                try
                {
                    return std::unique_ptr<NodePrx<RoboCompDSR::Content>>(new NodePrx<RoboCompDSR::Content>(&nodes.at(id)));
                }
                catch(const std::exception &e){ std::cout << "Graph::getNode Exception - id "<< id << " not found " << std::endl; throw e; };
            };
            RoboCompDSR::Content getNode(IDType id) const 
            { 
                try
                {
                    return nodes.at(id);
                }
                catch(const std::exception &e){ std::cout << "Graph::getNode Exception - id "<< id << " not found " << std::endl; throw e; };
            };
            void replaceNode(IDType id, const RoboCompDSR::Content &node)      
            { 
                try
                {
                    auto n = getNodePtr(id);
                    n->type = node.type;
                    n->id = id;
                    n->attrs = node.attrs;
                    n->fano = node.fano;
                }
                catch(const std::exception &e){ std::cout << "Graph::replaceNode Exception - id "<< id << " not found " << std::endl; throw e; };
            };
			void clear()                                            { nodes.clear();		}
            size_t size() const 								    { return nodes.size();  };

			///////////////////////////////////////////////////////////////////////////////////////////
			/// Printing and visitors
			///////////////////////////////////////////////////////////////////////////////////////////
			std::string printVisitor(const MTypes &t);
			void print();
			void readFromFile(const std::string &xml_file_path);

			////////////////////////////////////////////////////////////////////////////////////////////
			//								NOT	Thread safe graph API 
			////////////////////////////////////////////////////////////////////////////////////////////
			void addNode(IDType id, const std::string &type_) 	
			{ 
				RoboCompDSR::Content v; v.type = type_; v.id = id; nodes.insert(std::pair(id, v));
				nodes[id].attrs.insert(std::make_pair("name",std::string("unknown")));
				//emit addNodeSIGNAL(id, type_);
			};
			void addEdge(IDType from, IDType to, const std::string &label_) 			
			{ 
				nodes[from].fano.insert(std::pair(to, RoboCompDSR::EdgeAttribs{label_, from, to, RoboCompDSR::Attribs()}));
				//emit addEdgeSIGNAL(from, to, label_);
			};
			void addNodeAttribs(IDType id, const RoboCompDSR::Attribs &att)
			{ 
				for(auto &[k,v] : att)
					nodes[id].attrs.insert_or_assign(k,v);
				//emit NodeAttrsChangedSIGNAL(id, att);
			};
			
			void addEdgeAttribs(IDType from, IDType to, const RoboCompDSR::Attribs &att)  //HAY QUE METER EL TAG para desambiguar
			{ 
				auto &edgeAtts = nodes[from].fano.at(to);
				for(auto &[k,v] : att)
					edgeAtts.attrs.insert_or_assign(k,v);
				//std::cout << "Emit EdgeAttrsChangedSignal " << from << " to " << to << std::endl;
				//emit EdgeAttrsChangedSIGNAL(from, to);
			};
		//private:
            G nodes;    // Ice defined graph

            /// iterator so the class works in range loops only for private use
			typename G::iterator begin() 					{ return nodes.begin(); };
			typename G::iterator end() 						{ return nodes.end();   };
			typename G::const_iterator begin() const  		{ return nodes.begin(); };
			typename G::const_iterator end() const 	 		{ return nodes.begin(); };

		signals:
			void NodeAttrsChangedSIGNAL(const IDType, const RoboCompDSR::Attribs&);
			void EdgeAttrsChangedSIGNAL(const IDType, const DSR::IDType);
			void addNodeSIGNAL(const IDType, const std::string &type);
			void addEdgeSIGNAL(const IDType from, const DSR::IDType to, const std::string &label);
	};
};
#endif // GRAPH_H

//template <typename Ta> Ta getEdgeAttrib(IDType from, IDType to, const std::string &tag) const 	
			// { 	auto &attrs = nodes.at(from).fanout.at(to).attrs;
			// 	if(attrs.count(tag) > 0)
			// 		return std::get<Ta>(attrs.at(tag));
			// 	else return Ta();
			// };
			// Attribs getNodeAttrs(IDType id) const  	 								{ return nodes.at(id).attrs;};
			// Attribs& getEdgeAttrs(IDType from, IDType to) 							{ return nodes.at(from).fanout.at(to).attrs;};	
			// Attribs getEdgeAttrs(IDType from, IDType to) const						{ return nodes.at(from).fanout.at(to).attrs;};

			// //DEPRECATED
			// template<typename Ta>
			// Ta attr(const std::any &s) const     									{ return std::any_cast<Ta>(s);};
			
			// template<typename Ta>
			// Ta attr(const MTypes &s) const    	 									{ return std::get<Ta>(s);};
			
			// bool nodeExists(IDType id) const										{ return nodes.count(id) > 0;}
			
			// template<typename Ta>		
			// bool nodeHasAttrib(IDType id, const std::string &key, const std::string &value) const 
			// {  
			// 	auto &ats = nodes.at(id).attrs;
			// 	return (ats.count(key) > 0) and (this->attr<Ta>(ats.at(key))==value);
			// };
			
			// template<typename Ta>		
			// Ta getNodeAttribByName(IDType id, const std::string &key) const 
			// {  
			// 	return this->attr<Ta>(nodes.at(id).attrs.at(key));
			// };
			
			// std::vector<IDType> getEdgesByLabel(IDType id, const std::string &tag) 	
			// { 
			// 	std::vector<IDType> keys;
			// 	for(auto &[k, v] : nodes.at(id).fanout)
			// 	 	if( v.label == tag )
			// 	 		keys.push_back(k);
			// 	return keys;
    		// };
			
			// IDType getNodeByInnerModelName(const std::string &tag)
			// { 
			// 	if(tag == std::string())
			// 		return NO_PARENT;
			// 	for(auto &[k, v] : nodes)
			// 		if( attr<std::string>(v.attrs.at("imName")) == tag )
			// 			return k;
			// 	return NO_PARENT;  /// CHECK THIS IN ALL RESPONSES
			// };
			
			// std::int32_t getNodeLevel(IDType id)  									{ return getNodeAttribByName<std::int32_t>(id, "level");};
			// IDType getParentID(IDType id)  											{ return getNodeAttribByName<IDType>(id, "parent");};
			// std::string getNodeType(IDType id) const 								{ return nodes.at(id).type; }
