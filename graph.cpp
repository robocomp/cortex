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

#include "graph.h"
#include <fstream>
#include <QXmlSimpleReader>
#include <QXmlInputSource>
#include <QXmlDefaultHandler>
#include <libxml2/libxml/parser.h>
#include <libxml2/libxml/tree.h>

using namespace DSR;

void Graph::saveToFile(const std::string &xml_file_path)
{
    std::cout << __FUNCTION__ << "Saving graph to " << xml_file_path << std::endl;
	std::ofstream myfile;
	myfile.open (xml_file_path);
	myfile <<  "<AGMModel>\n\n";
    for( auto &[k, v] : nodes)
	{
		myfile <<"<symbol id=\"" << std::to_string(k) << "\" type=\"" << v.type << "\"";//>\n";
        if( v.attrs.size() > 0)
        {
           	myfile <<">\n";
			if( this->nodes.at(k).attrs.count("pos_x") == 0)
				myfile << "\t<attribute key=\"" << "pos_x" << "\" value=\""<< getNodeDrawAttribByName<float>(k, "posx") <<"\" />\n";
			if( this->nodes.at(k).attrs.count("pos_y") == 0)
				myfile << "\t<attribute key=\"" << "pos_y" << "\" value=\""<< getNodeDrawAttribByName<float>(k, "posy") <<"\" />\n";	
            for( auto &[ka, va] : v.attrs)
				myfile <<"\t<attribute key=\"" << ka <<"\" value=\"" << printVisitor(va) <<"\" />\n";		
			myfile <<"</symbol>\n";
		}
		else
			myfile <<"/>\n";
	}
	myfile <<"\n\n";
	for ( auto &[node_key, node_value] : nodes)	
    {
        for( auto &[to_edge, edge_value] : node_value.fanout)
        {
		    //myfile << "<link src=\"" << edges[i].symbolPair.first << "\" dst=\"" << edges[i].symbolPair.second << "\" label=\""<<edges[i].linking<<"\"";//> \n";
            myfile << "<link src=\"" << node_key << "\" dst=\"" << to_edge << "\" label=\"" << edge_value.label <<"\"";//> \n";
		    if ( edge_value.attrs.size() > 0)
			{
				myfile <<">\n";
				if(edge_value.label == "RT")
				{
					RTMat mat = this->attr<RTMat>(edge_value.attrs["RT"]);
					myfile << "\t<linkAttribute key=\"" << "tx" << "\" value=\""<< mat.getTr().x() <<"\" />\n";
					myfile << "\t<linkAttribute key=\"" << "ty" << "\" value=\""<< mat.getTr().y() <<"\" />\n";
					myfile << "\t<linkAttribute key=\"" << "tz" << "\" value=\""<< mat.getTr().z() <<"\" />\n";
					QVec ang = mat.extractAnglesR_min();
					myfile << "\t<linkAttribute key=\"" << "rx" << "\" value=\""<< ang.x() <<"\" />\n";
					myfile << "\t<linkAttribute key=\"" << "ry" << "\" value=\""<< ang.y() <<"\" />\n";
					myfile << "\t<linkAttribute key=\"" << "rz" << "\" value=\""<< ang.z() <<"\" />\n";
				}
				else
				{
					for( auto &[attr_name, attr_value] : edge_value.attrs)
						myfile << "\t<linkAttribute key=\"" << attr_name << "\" value=\""<< printVisitor(attr_value) <<"\" />\n";
				}
				myfile <<"</link>\n";
			}
		    else
			    myfile <<"/>\n";
        }
	}
	myfile <<  "\n</AGMModel>\n";
	myfile.close();
    std::cout << __FUNCTION__ << "File saved to " << xml_file_path << std::endl;
}

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
			this->addNodeAttribs(node_id, DSR::Attribs{ 
			 					std::pair("level", std::int32_t(0)),
			 					std::pair("parent", IDType(0)),
								//std::pair("pos_x", rd[0]),
								//std::pair("pos_y", rd[1]),
								 });
	
			// Draw attributes come now
			DSR::DrawAttribs atts;
			DSR::Attribs gatts;
			std::string qname = (char *)stype;
			std::string full_name = std::string((char *)stype) + " [" + std::string((char *)sid) + "]";
			atts.insert(std::pair("name", full_name));
			
			// color selection
			std::string color = "coral";
			if(qname == "world") color = "SeaGreen";
			else if(qname == "transform") color = "SteelBlue";
			else if(qname == "plane") color = "Khaki";
			else if(qname == "differentialrobot") color = "GoldenRod";
			else if(qname == "laser") color = "GreenYellow";
			else if(qname == "mesh") color = "LightBlue";
			else if(qname == "imu") color = "LightSalmon";
			
			atts.insert(std::pair("color", color));
			gatts.insert(std::pair("color", color));
			this->addNodeDrawAttribs(node_id, atts);  //DEPREC
			this->addNodeAttribs(node_id, gatts);
			std::cout << node_id << " " <<  std::string((char *)stype) << std::endl;
			
			xmlFree(sid);
			xmlFree(stype);

			for (xmlNodePtr cur2=cur->xmlChildrenNode; cur2!=NULL; cur2=cur2->next)
			{
				if (xmlStrcmp(cur2->name, (const xmlChar *)"attribute") == 0)
				{
					xmlChar *attr_key   = xmlGetProp(cur2, (const xmlChar *)"key");
					xmlChar *attr_value = xmlGetProp(cur2, (const xmlChar *)"value");
					
					//s->setAttribute(std::string((char *)attr_key), std::string((char *)attr_value));
					std::string sk = std::string((char *)attr_key);
					if( sk == "level" or sk == "parent")
						this->addNodeAttribs(node_id, DSR::Attribs{ std::pair(sk, std::stoi(std::string((char *)attr_value)))});
					else if( sk == "pos_x" or sk == "pos_y")
						this->addNodeAttribs(node_id, DSR::Attribs{ std::pair(sk, (float)std::stod(std::string((char *)attr_value)))});
					else 
						this->addNodeAttribs(node_id, DSR::Attribs{ std::pair(sk, std::string((char *)attr_value))});
							
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
			
			this->addEdge(a, b, edgeName);
			this->addEdgeDrawAttribs(a, b, DSR::DrawAttribs{std::pair("name", edgeName)}); //DEPREC
			this->addEdgeAttribs(a, b, DSR::Attribs{std::pair("name", edgeName)});
			
 			DSR::Attribs edge_attribs;
			if( edgeName == "RT")   //add level to node b as a.level +1, and add parent to node b as a
			{ 	
				this->addNodeAttribs(b, DSR::Attribs{ std::pair("level", this->getNodeLevel(a)+1), std::pair("parent", a)});
				RMat::RTMat rt;
				float x,y,z;
				for(auto &[k,v] : attrs)
				{
					if(k=="tx")	( x = std::stof(v) );
					if(k=="ty")	( y = std::stof(v) );
					if(k=="tz")	( z = std::stof(v) );
					rt.setTr( x, y, z);
					if(k=="rx")	rt.setRX(std::stof(v));
					if(k=="ry")	rt.setRY(std::stof(v));
					if(k=="rz")	rt.setRZ(std::stof(v));
 					this->addEdgeAttribs(a, b, DSR::Attribs{std::pair("RT", rt)});
				}
			}
			else
			{
				this->addNodeAttribs(b, DSR::Attribs{ std::pair("parent", 0)});
				for(auto &r : attrs)
 					edge_attribs.insert(r);
			}

			std::cout << __FUNCTION__ << "edge  from " << a << " " <<  edgeName  << std::endl;		
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

void Graph::print()
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


/////////////////////////77
/// OLD

// void SpecificWorker::initializeFromInnerModel(InnerModel *inner)
// {
// 	this->walkTree(inner->getRoot());
// }

// void SpecificWorker::walkTree(InnerModelNode *node)
// {
// 	static uint16_t node_id = 0;
// 	if (node == nullptr)
// 		node = innerModel->getRoot();
	
// 	graph->addNode(node_id, "transform");
// 	auto node_parent = node_id;
// 	DSR::DrawAttribs atts;
// 	auto rd = QVec::uniformVector(2,0,3000);
// 	auto ids = node->id.toStdString();
// 	atts.insert(std::pair("name", ids));
// 	atts.insert(std::pair("posx", rd[0]));
// 	atts.insert(std::pair("posy", rd[1]));
// 	if(ids == "base")
// 		atts.insert(std::pair("color", std::string{"coral"}));
// 	if(ids == "laser")
// 		atts.insert(std::pair("color", std::string{"darkgreen"}));
// 	else
// 		atts.insert(std::pair("color", std::string{"steelblue"}));
		
// 	graph->addNodeDrawAttribs(node_id, atts);
// 	node_id++;
	
// 	for(auto &it : node->children)	
// 	{	
// 		graph->addEdge(node_parent, node_id, "is_a");
// 		graph->addEdgeDrawAttribs(node_parent, node_id, DSR::DrawAttribs{std::pair("name", std::string{"parentOf"})});
// 		graph->addEdge(node_id, node_parent,"has_a");
// 		graph->addEdgeDrawAttribs(node_id, node_parent, DSR::DrawAttribs{std::pair("name", std::string{"childOf"})});
// 		walkTree(it);
// 	}
// }

// void SpecificWorker::initializeRandom()
// {
// 	std::cout << "Initialize random" << std::endl;
	
// 	//Crear el grafo
// 	srand (time(NULL));
// 	const int nNodes = 2;
// 	for (auto i : iter::range(nNodes)) 
// 	{
// 		graph->addNode(i, "transform"); 
// 		DSR::DrawAttribs atts;
// 		auto rd = QVec::uniformVector(2,-200,200);
// 		atts.insert(std::pair("posx", rd[0]));
// 		atts.insert(std::pair("posy", rd[1]));
// 		atts.insert(std::pair("name", std::string("mynode")));
// 		atts.insert(std::pair("color", std::string("red")));
// 		graph->addNodeDrawAttribs(i, atts);
// 	}
// 	for (auto i : iter::range(nNodes)) 
// 	{
// 		auto rd = QVec::uniformVector(2,0,nNodes);
// 		graph->addEdge(rd[0], rd[1], "is_a");
// 		graph->addEdgeDrawAttribs(rd[0], rd[1], DSR::DrawAttribs{std::pair("name", std::string("edge"))});
// 	}
// }