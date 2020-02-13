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

#include "graphNEW.h"
#include <fstream>
#include <QXmlSimpleReader>
#include <QXmlInputSource>
#include <QXmlDefaultHandler>
#include <libxml2/libxml/parser.h>
#include <libxml2/libxml/tree.h>

using namespace DSR;

void Graph::readFromFile(const std::string &file_name)
{
	//version = 0;
	std::cout << __FUNCTION__ << "Reading xml file: " << file_name << std::endl;

	// Open file and make initial checks
	xmlDocPtr doc;
	if ((doc = xmlParseFile(file_name.c_str())) == NULL)
	{
		fprintf(stderr,"Can't read XML file - probably a syntax error. \n");
		exit(1);
	}
	xmlNodePtr root;
	if ((root = xmlDocGetRootElement(doc)) == NULL)
	{
		fprintf(stderr,"Can't read XML file - empty document\n");
		xmlFreeDoc(doc);
		exit(1);
	}
	if (xmlStrcmp(root->name, (const xmlChar *) "AGMModel"))
	{
		fprintf(stderr,"Can't read XML file - root node != AGMModel");
		xmlFreeDoc(doc);
		exit(1);
	}

	// Read symbols (just symbols, then links in other loop)
	for (xmlNodePtr cur=root->xmlChildrenNode; cur!=NULL; cur=cur->next)
	{
		if (xmlStrcmp(cur->name, (const xmlChar *)"symbol") == 0)
		{
			xmlChar *stype = xmlGetProp(cur, (const xmlChar *)"type");
			xmlChar *sid = xmlGetProp(cur, (const xmlChar *)"id");
			
			//AGMModelSymbol::SPtr s = newSymbol(atoi((char *)sid), (char *)stype);
			IDType node_id = std::atoi((char *)sid);
			std::string node_type((char *)stype);
			auto rd = QVec::uniformVector(2,-200,200);
			this->addNode(node_id, node_type);
			this->addNodeAttribs(node_id, RoboCompDSR::Attribs{ 
			 					//std::pair("level", std::int32_t(0)),
			 					//std::pair("parent", IDType(0)),
								std::pair("level", "0"),
			 					std::pair("parent", "0"),
								 
								//std::pair("pos_x", rd[0]),
								//std::pair("pos_y", rd[1]),
								 });
	
			// Draw attributes come now
			RoboCompDSR::Attribs gatts;
			std::string qname = (char *)stype;
			std::string full_name = std::string((char *)stype) + " [" + std::string((char *)sid) + "]";
			gatts.insert(std::pair("name", full_name));
			// color selection
			std::string color = "coral";
			if(qname == "world") color = "SeaGreen";
			else if(qname == "transform") color = "SteelBlue";
			else if(qname == "plane") color = "Khaki";
			else if(qname == "differentialrobot") color = "GoldenRod";
			else if(qname == "laser") color = "GreenYellow";
			else if(qname == "mesh") color = "LightBlue";
			else if(qname == "imu") color = "LightSalmon";
			
			gatts.insert(std::pair("color", color));
			
			this->addNodeAttribs(node_id, gatts);
			

			std::cout << __FILE__ << " " << __FUNCTION__ << "Node: " << node_id << " " <<  std::string((char *)stype) << std::endl;
			
			xmlFree(sid);
			xmlFree(stype);

			for (xmlNodePtr cur2=cur->xmlChildrenNode; cur2!=NULL; cur2=cur2->next)
			{
				if (xmlStrcmp(cur2->name, (const xmlChar *)"attribute") == 0)
				{
					xmlChar *attr_key   = xmlGetProp(cur2, (const xmlChar *)"key");
					xmlChar *attr_value = xmlGetProp(cur2, (const xmlChar *)"value");
					std::string sk = std::string((char *)attr_key);

					if( sk == "level" or sk == "parent")
						//this->addNodeAttribs(node_id, DSR::Attribs{ std::pair(sk, std::stoi(std::string((char *)attr_value)))});
						this->addNodeAttribs(node_id, RoboCompDSR::Attribs{ std::pair(sk, std::string((char *)attr_value))});
					else if( sk == "pos_x" or sk == "pos_y")
						//this->addNodeAttribs(node_id, DSR::Attribs{ std::pair(sk, (float)std::stod(std::string((char *)attr_value)))});
						this->addNodeAttribs(node_id, RoboCompDSR::Attribs{ std::pair(sk, (std::string((char *)attr_value)))});
					else 
						this->addNodeAttribs(node_id, RoboCompDSR::Attribs{ std::pair(sk, std::string((char *)attr_value))});
							
					xmlFree(attr_key);
					xmlFree(attr_value);
				}
				else if (xmlStrcmp(cur2->name, (const xmlChar *)"comment") == 0) { }           // coments are always ignored
				else if (xmlStrcmp(cur2->name, (const xmlChar *)"text") == 0) { }     // we'll ignore 'text'
				else { printf("unexpected tag inside symbol: %s\n", cur2->name); exit(-1); } // unexpected tags make the program exit
			}
		}
		else if (xmlStrcmp(cur->name, (const xmlChar *)"link") == 0) { }     // we'll ignore links in this first loop
		else if (xmlStrcmp(cur->name, (const xmlChar *)"text") == 0) { }     // we'll ignore 'text'
		else if (xmlStrcmp(cur->name, (const xmlChar *)"comment") == 0) { }  // coments are always ignored
		else { printf("unexpected tag #1: %s\n", cur->name); exit(-1); }      // unexpected tags make the program exit
	}		

	// Read links
	for (xmlNodePtr cur=root->xmlChildrenNode; cur!=NULL; cur=cur->next)
	{
		if (xmlStrcmp(cur->name, (const xmlChar *)"link") == 0)
		{
			xmlChar *srcn = xmlGetProp(cur, (const xmlChar *)"src");
			if (srcn == NULL) { printf("Link %s lacks of attribute 'src'.\n", (char *)cur->name); exit(-1); }
			IDType a = atoi((char *)srcn);
			xmlFree(srcn);

			xmlChar *dstn = xmlGetProp(cur, (const xmlChar *)"dst");
			if (dstn == NULL) { printf("Link %s lacks of attribute 'dst'.\n", (char *)cur->name); exit(-1); }
			IDType b = atoi((char *)dstn);
			xmlFree(dstn);

			xmlChar *label = xmlGetProp(cur, (const xmlChar *)"label");
			if (label == NULL) { printf("Link %s lacks of attribute 'label'.\n", (char *)cur->name); exit(-1); }
			std::string edgeName((char *)label);
			xmlFree(label);

			std::map<std::string, std::string> attrs;
			for (xmlNodePtr cur2=cur->xmlChildrenNode; cur2!=NULL; cur2=cur2->next)
			{
				if (xmlStrcmp(cur2->name, (const xmlChar *)"linkAttribute") == 0)
				{
					xmlChar *attr_key   = xmlGetProp(cur2, (const xmlChar *)"key");
					xmlChar *attr_value = xmlGetProp(cur2, (const xmlChar *)"value");
					attrs[std::string((char *)attr_key)] = std::string((char *)attr_value);
					xmlFree(attr_key);
					xmlFree(attr_value);
				}
				else if (xmlStrcmp(cur2->name, (const xmlChar *)"comment") == 0) { }           // coments are always ignored
				else if (xmlStrcmp(cur2->name, (const xmlChar *)"text") == 0) { }     // we'll ignore 'text'
				else { printf("unexpected tag inside symbol: %s ==> %s\n", cur2->name,xmlGetProp(cur2, (const xmlChar *)"id") ); exit(-1); } // unexpected tags make the program exit
			}
			
			std::cout << __FILE__ << " " << __FUNCTION__ << "Edge from " << a << " to " << b << " label "  << edgeName <<  std::endl;
			this->addEdge(a, b, edgeName);
			this->addEdgeAttribs(a, b, RoboCompDSR::Attribs{std::pair("name", edgeName)});
			
 			RoboCompDSR::Attribs edge_attribs;
			if( edgeName == "RT")   //add level to node b as a.level +1, and add parent to node b as a
			{ 	
				std::string level = std::to_string(std::stoi(nodes.at(a).attrs.at("level")) +1 );
				this->addNodeAttribs(b, RoboCompDSR::Attribs{ std::pair("level", level), std::pair("parent", std::to_string(a)) });
				//RMat::RTMat rt;
				std::string tx,ty,tz,rx,ry,rz;
				std::string srt;
				for(auto &[k,v] : attrs)
				{
					if(k=="tx")	tx = v;
					if(k=="ty")	ty = v;
					if(k=="tz")	tz = v;
					if(k=="rx")	rx = v;
					if(k=="ry")	ry = v;
					if(k=="rz")	rz = v;
				}
				//rt.set(rx, ry, rz, tx, ty, tz);
				//rt.print("in reader");
				srt = tx  + " " + ty + " " + tz + " " + rx + " " + ry + " " + rz;
 				this->addEdgeAttribs(a, b, RoboCompDSR::Attribs{std::pair("RT", srt)});
			}
			else
			{
				this->addNodeAttribs(b, RoboCompDSR::Attribs{ std::pair("parent", "0")});
				for(auto &r : attrs)
 					edge_attribs.insert(r);
			}	
 			this->addEdgeAttribs(a, b, edge_attribs);
			
	//	  this->addEdgeByIdentifiers(a, b, edgeName, attrs);
		}
		else if (xmlStrcmp(cur->name, (const xmlChar *)"symbol") == 0) { }   // symbols are now ignored
		else if (xmlStrcmp(cur->name, (const xmlChar *)"text") == 0) { }     // we'll ignore 'text'
		else if (xmlStrcmp(cur->name, (const xmlChar *)"comment") == 0) { }  // comments are always ignored
		else { printf("unexpected tag #2: %s\n", cur->name); exit(-1); }      // unexpected tags make the program exit
	}
}

///////////////////////////////////////////////////////////////////////////////////////////
/// Printing and visitors
///////////////////////////////////////////////////////////////////////////////////////////
std::string Graph::printVisitor(const MTypes &t)
{			
	return std::visit(overload
	{
		[](RMat::RTMat m) -> std::string								{ return m.serializeAsString(); },
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

void Graph::print()
{
	std::cout << "------------Printing Graph: " << nodes.size() << " elements -------------------------" << std::endl;
	for( auto &[k,v] : nodes)
	{
		std::cout << "[" << v.attrs["name"] << "] : " << std::endl;
		std::cout << "	attrs:	";
		for( auto &[ka, kv] : v.attrs)
		{
			std::cout << ka << " -> " << kv  << " , ";
		}
		std::cout << std::endl << "	edges:	";
		for( auto &[kf,vf] : v.fano)
		{
			std::cout << vf.label << "( from " << k << " to " << kf  << " ) " << std::endl;
			std::cout << "			edge attrs: ";
			for( auto &[ke, ve] : vf.attrs)
			{
				std::cout << ke << " -> " << ve << " , ";
			}
			std::cout << std::endl << "		";
		}
		std::cout << std::endl;
	}
	std::cout << "---------------- graph ends here --------------------------" << std::endl;
}
