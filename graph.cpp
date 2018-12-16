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

using namespace DSR;

void Graph::save(const std::string &xml_file_path)
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
                for( auto &[attr_name, attr_value] : edge_value.attrs)
				    myfile << "\t<linkAttribute key=\"" << attr_name << "\" value=\""<< printVisitor(attr_value) <<"\" />\n";
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

