/*
 * Copyright 2018 <copyright holder> <email>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef GRAPH_H
#define GRAPH_H

#include <iostream>
#include <unordered_map>
#include <any>
#include <memory>
#include <vector>
#include <variant>
#include <qmat/QMatAll>

#define NO_PARENT -1

// Overload pattern used inprintVisitor
template<class... Ts> struct overload : Ts... { using Ts::operator()...; };
template<class... Ts> overload(Ts...) -> overload<Ts...>;

namespace DSR
{
	using IDType = std::int32_t;
	using IMType = QString;
	
	using MTypes = std::variant<std::uint32_t, std::int32_t, float, std::string, std::vector<float>, RMat::RTMat>;		
	using Attribs = std::unordered_map<std::string, MTypes>;
	using DrawAttribs = std::unordered_map<std::string, std::any>;
	struct EdgeAttrs
	{ 
		std::string label;
		IDType from, to;
		Attribs attrs; 
		DrawAttribs draw_attrs;
	};
	using FanOut = std::unordered_map<IDType, EdgeAttrs>;
	using FanIn = std::vector<IDType>;

	class Graph 
	{
		public:
			 struct Value
			{
				std::string type;
				IDType id;
				Attribs attrs;
				DrawAttribs draw_attrs;
				FanOut fanout;
				FanIn fanin;
			}; 
			using Nodes = std::unordered_map<IDType, Value>;
			
			////////////////////////////////////////////////////////////////////////////////////////////
			//									Graph API 
			////////////////////////////////////////////////////////////////////////////////////////////
			
			typename Nodes::iterator begin() 					{ return nodes.begin(); };
			typename Nodes::iterator end() 						{ return nodes.end();   };
			typename Nodes::const_iterator begin() const  		{ return nodes.begin(); };
			typename Nodes::const_iterator end() const 	 		{ return nodes.begin(); };
			
			size_t size() const 								{ return nodes.size();  };

			//////////////////////////////////////////////////////////////////////////////////////////////////////
			///// Graph editing methods. These should be intercepted and publishd to ether
			///////////////////////////////////////////////////////////////////////////////////////////////////////
			void addNode(IDType id, const std::string &type_) 	{ Value v; v.type = type_; v.id = id; nodes.insert(std::pair(id, v));};
			void addEdge(IDType from, IDType to, const std::string &label_) 			
			{ 
				nodes[from].fanout.insert(std::pair(to, EdgeAttrs{label_, from, to, Attribs(), DrawAttribs()}));
				nodes[to].fanin.push_back(from);
			};
			void addNodeAttribs(IDType id, const Attribs &att)
			{ 
				for(auto &[k,v] : att)
					nodes[id].attrs.insert_or_assign(k,v);
			};
			void addNodeDrawAttribs(IDType id, const DrawAttribs &att)
			{ 
				for(auto &[k,v] : att)
					nodes[id].draw_attrs.insert_or_assign(k,v);
			};
			void addEdgeAttribs(IDType from, IDType to, const Attribs &att)
			{ 
					auto &edgeAtts = nodes[from].fanout.at(to);
					for(auto &[k,v] : att)
						edgeAtts.attrs.insert_or_assign(k,v);
			};
			void addEdgeDrawAttribs(IDType from, IDType to, const DrawAttribs &att)
			{ 
					auto &edgeAtts = nodes[from].fanout.at(to);
					for(auto &[k,v] : att)
						edgeAtts.draw_attrs.insert_or_assign(k,v);
			};
			///////////////////////////////////////////////////////////////////////////////////////////

			std::string printVisitor(const MTypes &t)
			{			
				return std::visit(overload
				{
    				[](RTMat m) -> std::string										{ return m.asQString("RT").toStdString(); },
    				[](std::vector<float> a)-> std::string	
					{ 
						std::string str;
						for(auto &f : a)
							str += std::to_string(f) + " ";
						return  str += "\n"; 
					},
					[](std::string a) -> std::string								{ return  a; },
					[](auto a) -> std::string										{ return std::to_string(a);} 
				}, t);
    		};

			void print()
			{
				std::cout << "------------Printing Graph: " << nodes.size() << " elements -------------------------" << std::endl;
				for( auto &par : nodes)
				{
					auto &v = par.second;
					std::cout << "[" << attr<std::string>(v.draw_attrs["name"]) << "] : " << std::endl;
					std::cout << "	attrs:	";
					for( auto &[ka, kv] : v.attrs)
					{
						std::cout << ka << " -> " << printVisitor(kv)  << " , ";
					}
					std::cout << std::endl << "	edges:	";
					for( auto &[kf,vf] : v.fanout)
					{
						std::cout << vf.label << "( " << attr<std::string>(nodes[kf].draw_attrs["name"])  << " ) " << std::endl;
						std::cout << "			edge attrs: ";
						for( auto &[ke, ve] : vf.attrs)
						{
							std::cout << ke << " -> " << printVisitor(ve) << " , ";
						}
						std::cout << std::endl << "		";
					}
					std::cout << std::endl;
				}
				std::cout << "---------------- graph ends here --------------------------" << std::endl;
			}

			FanOut fanout(IDType id) const   										{ return nodes.at(id).fanout;};
			FanOut& fanout(IDType id)            									{ return nodes.at(id).fanout;};
			template <typename Ta> Ta getEdgeAttrib(IDType from, IDType to, const std::string &tag) const 	
			{ 	auto &attrs = nodes.at(from).fanout.at(to).attrs;
				if(attrs.count(tag) > 0)
					return std::get<Ta>(attrs.at(tag));
				else return Ta();
			};
			// Attribs attrs(IDType id) const  	 									{ return nodes.at(id).attrs;};
			// Attribs& attrs(IDType id)       										{ return nodes.at(id).attrs;};
			DrawAttribs getNodeDrawAttrs(IDType id) const							{ return nodes.at(id).draw_attrs;};
			DrawAttribs& getNodeDrawAttrs(IDType id)      							{ return nodes.at(id).draw_attrs;};
			//Attribs& getEdgeAttrs(IDType from, IDType to) 							{ return nodes.at(from).fanout.at(to).attrs;};
			//Attribs getEdgeAttrs(IDType from,  IDType to) const						{ return nodes.at(from).fanout.at(to).attrs;};
			
			template<typename Ta>
			Ta attr(const std::any &s) const     									{ return std::any_cast<Ta>(s);};

			template<typename Ta>
			Ta attr(const MTypes &s) const    	 									{ return std::get<Ta>(s);};
			
			bool nodeExists(IDType id) const										{ return nodes.count(id) > 0;}
			template<typename Ta>		
			bool nodeHasAttrib(IDType id, const std::string &key, const std::string &value) const 
			{  
				auto &ats = nodes.at(id).attrs;
				return (ats.count(key) > 0) and (this->attr<Ta>(ats.at(key))==value);
			};
			template<typename Ta>		
			T getNodeAttribByName(IDType id, const std::string &key) const 
			{  
				return this->attr<Ta>(nodes.at(id).attrs.at(key));
			};
			std::vector<IDType> getEdgesByLabel(IDType id, const std::string &tag) 	
			{ 
				std::vector<IDType> keys;
				for(auto &[k, v] : nodes.at(id).fanout)
				 	if( v.label == tag )
				 		keys.push_back(k);
				return keys;
    		};
			IDType getNodeByInnerModelName(const std::string &key, const std::string &tag)
			{ 
				if(tag == std::string())
					return NO_PARENT;
				for(auto &[k, v] : nodes)
					if( attr<std::string>(v.attrs.at("imName")) == tag )
						return k;
				return NO_PARENT;  /// CHECK THIS IN ALL RESPONSES
			};
			std::int32_t getNodeLevel(IDType id)  									{ return getNodeAttribByName<std::int32_t>(id, "level");};
			IDType getParentID(IDType id)  											{ return getNodeAttribByName<IDType>(id, "parent");};
		
		private:
			Nodes nodes;

			FanIn fanin(IDType id) const    										{ return nodes.at(id).fanin;};
			FanIn& fanin(IDType id)             				   					{ return nodes.at(id).fanin;};
	};
}
#endif // GRAPH_H




//FUSCA
	// template <typename Ta> 
	// 		Ta attribFanin(IDType to, const std::string &tag) const 	
	// 		{ 	auto &fin = nodes.at(to).fanin;
	// 			auto edge = std::find_if(fin.begin(), fin.end(), [to, tag, this](auto from){ return edgeAttrs(from, to).count(tag)>0;});
	// 			if(edge != fin.end())
	// 				return std::get<Ta>(edgeAttrs(*edge,to).at(tag));
	// 			else return Ta();
	// 		};

	/* class Value
	{
		public:
			Attribs &attrs() 			{ return node_attrs;};
			DrawAttribs &drawAttrs() 	{ return node_draw_attrs;};
			FanOut &fanout() 			{ return node_fanout;};
			FanIn &fanin()	 			{ return node_fanin;};
		
		private:
			Attribs node_attrs;
			DrawAttribs node_draw_attrs;
			FanOut node_fanout;
			FanIn node_fanin;
	}; */