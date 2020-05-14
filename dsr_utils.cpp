////////////////////////////////////////////////////////////////////////
//// UTILITIES FOR DSRGraph
////////////////////////////////////////////////////////////////////////

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <fstream>
#include "dsr_utils.h"
#include "CRDT.h"

using namespace CRDT;

Utilities::Utilities(CRDT::CRDTGraph *G_)
{ G = G_; }

void Utilities::read_from_json_file(const std::string &json_file_path)
{
    std::cout << __FUNCTION__ << " Reading json file: " << json_file_path << std::endl;

    // Open file and make initial checks
    QFile file;
    file.setFileName(QString::fromStdString(json_file_path));
    if (not file.open(QIODevice::ReadOnly | QIODevice::Text))
        throw std::runtime_error("File " + json_file_path + " not found. Cannot continue.");
    
    QString val = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(val.toUtf8());
    QJsonObject jObject = doc.object();

    QJsonObject dsrobject = jObject.value("DSRModel").toObject();
	QJsonObject symbolMap = dsrobject.value("symbol").toObject();
    QJsonArray linksArray;
    // Read symbols (just symbols, then links in other loop)
    foreach (const QString &key, symbolMap.keys())
    {
        QJsonObject sym_obj = symbolMap[key].toObject();
        int id = sym_obj.value("id").toString().toInt();
        std::string type = sym_obj.value("type").toString().toStdString();
        std::string name = sym_obj.value("name").toString().toStdString();
       
        if (id == -1) 
        {
            std::cout << __FILE__ << " " << __FUNCTION__ << " Invalid ID Node: " << std::to_string(id);
            continue;
        }
        std::cout << __FILE__ << " " << __FUNCTION__ << ", Node: " << std::to_string(id) << " " <<  type << std::endl;
        Node n;
        n.type(type);
        n.id(id);
        n.agent_id(G->get_agent_id());
        n.name(name);
        G->name_map[name] = id;
        G->id_map[id] = name;

        std::map<string, Attrib> attrs;
        // G->add_attrib(attrs, "level",std::int32_t(0));
        // G->add_attrib(attrs, "parent",std::int32_t(0));
        G->insert_or_assign_attrib_by_name(n, "level", std::int32_t(0));
        G->insert_or_assign_attrib_by_name(n, "parent", std::int32_t(0));
        
        std::string full_name = type + " [" + std::to_string(id) + "]";
        G->insert_or_assign_attrib_by_name(n, "name", full_name);
        // G->add_attrib(attrs, "name",full_name);

        // color selection
        std::string color = "coral";
        if(type == "world") color = "SeaGreen";
        else if(type == "transform") color = "SteelBlue";
        else if(type == "plane") color = "Khaki";
        else if(type == "differentialrobot") color = "GoldenRod";
        else if(type == "laser") color = "GreenYellow";
        else if(type == "mesh") color = "LightBlue";
        else if(type == "imu") color = "LightSalmon";

        //G->add_attrib(attrs, "color", color);
        G->insert_or_assign_attrib_by_name(n, "color", color);

        // node atributes
        QVariantMap attributesMap = sym_obj.value("attribute").toObject().toVariantMap();
        for(QVariantMap::const_iterator iter = attributesMap.begin(); iter != attributesMap.end(); ++iter)
        {
            std::string attr_key = iter.key().toStdString();
            QString attr_value = iter.value().toMap()["value"].toString();
            int attr_type = iter.value().toMap()["type"].toString().toInt();

            switch (attr_type) {
                case 0:
                    //G->add_attrib(attrs, attr_key, attr_value.toStdString());
                    G->insert_or_assign_attrib_by_name(n, attr_key, attr_value.toStdString());
                    break;
                case 1:
                    //G->add_attrib(attrs, attr_key, attr_value.toInt());
                    G->insert_or_assign_attrib_by_name(n, attr_key, attr_value.toInt());
                    break;
                case 2:
                    //G->add_attrib(attrs, attr_key, attr_value.replace(",", ".").toFloat());
                    G->insert_or_assign_attrib_by_name(n, attr_key, attr_value.replace(",", ".").toFloat());
                    break;
                case 3: 
                {
                    std::vector<float> v;
                    std::istringstream iss(attr_value.toStdString());
                    std::copy(std::istream_iterator<float>(iss),
                              std::istream_iterator<float>(),
                              std::back_inserter(v));
                    G->insert_or_assign_attrib_by_name(n, attr_key, v);
                    break;
                }
                case 4: 
                {
                    G->insert_or_assign_attrib_by_name(n, attr_key, attr_value.contains("true"));
                    break;
                }
                default:
                   G->insert_or_assign_attrib_by_name(n, attr_key, attr_value.toStdString());
            }
        }
        //n.attrs(attrs);
        G->insert_or_assign_node(n);
        // get links
        QJsonArray nodeLinksArray = sym_obj.value("links").toArray();
        std::copy(nodeLinksArray.begin(), nodeLinksArray.end(), std::back_inserter(linksArray));
    }
    // Read links
    foreach (const QJsonValue & linkValue, linksArray)
    {
        QJsonObject link_obj = linkValue.toObject();
        int srcn = link_obj.value("src").toString().toInt();
        int dstn = link_obj.value("dst").toString().toInt();
        std::string edgeName = link_obj.value("label").toString().toStdString();
        std::map<string, Attrib> attrs;
        Edge edge;
        edge.from(srcn);
        edge.to(dstn);
        edge.type(edgeName);

        // link atributes
        QVariantMap linkAttributesMap = link_obj.value("linkAttribute").toObject().toVariantMap();
        for(QVariantMap::const_iterator iter = linkAttributesMap.begin(); iter != linkAttributesMap.end(); ++iter)
        {
            std::string attr_key = iter.key().toStdString();
            QVariant attr_value = iter.value().toMap()["value"];
            int attr_type = iter.value().toMap()["type"].toString().toInt();

            Attrib av;
            av.type(attr_type);
            Val value;

            switch (attr_type) 
            {
                case 0:
                {
                    G->insert_or_assign_attrib_by_name(edge, attr_key, attr_value.toString().toStdString());
                    break;
                }
                case 1:
                {
                    G->insert_or_assign_attrib_by_name(edge, attr_key, attr_value.toInt());
                    break;
                }
                case 2:
                {
                    G->insert_or_assign_attrib_by_name(edge, attr_key, attr_value.toString().replace(",", ".").toFloat());
                    break;
                }
                case 3: 
                {
                    std::vector<float> v;
                    foreach (const QVariant& value, attr_value.toList())
                        v.push_back(value.toFloat());    
                    G->insert_or_assign_attrib_by_name(edge, attr_key, v);
                    break;
                }
                case 4: 
                {
                    G->insert_or_assign_attrib_by_name(edge, attr_key, attr_value.toString().contains("true"));
                    break;
                }
            }
        }
        std::cout << __FILE__ << " " << __FUNCTION__ << "Edge from " << std::to_string(srcn) << " to " << std::to_string(dstn) << " label "  << edgeName <<  std::endl;
        //edge.attrs(attrs);
        G->insert_or_assign_edge(edge);

    } //foreach(links)
}

void Utilities::write_to_json_file(const std::string &json_file_path)
{
    std::setlocale(LC_NUMERIC, "en_US.UTF-8");
    //create json object
    QJsonObject dsrObject;
    QJsonArray linksArray;
    QJsonObject symbolsMap;
    for (auto kv : G->nodes.getMapRef()) {
        Node node = kv.second.dots().ds.rbegin()->second;
        // symbol data
        QJsonObject symbol;
        symbol["id"] = QString::number(node.id());
        symbol["type"] = QString::fromStdString(node.type());
        symbol["name"] = QString::fromStdString(node.name());
        // symbol attribute
        QJsonObject attrsObject;
        for (const auto &[key, value]: node.attrs())
        {
            std::ostringstream vf;

            QJsonObject content;
            std::string val;

            switch (value.value()._d()) {
                case 0:
                    val = value.value().str();
                    break;
                case 1:
                    val = std::to_string(value.value().dec());
                    break;
                case 2:
                    val = std::to_string(value.value().fl());
                    break;
                case 3:
                    if (!value.value().float_vec().empty())
                    {
                        std::copy(value.value().float_vec().begin(), value.value().float_vec().end()-1,
                                  std::ostream_iterator<float>(vf, ", "));

                        vf << value.value().float_vec().back();
                    }
                    val = vf.str();
                    break;
                case 4:
                    val = "false";
                    if (value.value().bl())
                        val = "true";
                    break;
            }
            content["type"] = QString::number(value.value()._d());
            content["value"] = QString::fromStdString(val);
            attrsObject[QString::fromStdString(key)] = content;
        }
        symbol["attribute"] = attrsObject;
        //link
        QJsonArray nodeLinksArray;
        for (const auto &[key, value]: node.fano()) {
            QJsonObject link;
            link["src"] = QString::number(value.from());
            link["dst"] = QString::number(value.to());
            link["label"] = QString::fromStdString(value.type());
            // link attribute
            QJsonObject lattrsObject;
            for (const auto &[key, value]: value.attrs()) {
                std::ostringstream vf;

                QJsonObject attr, content;
                QJsonValue val;
                switch (value.value()._d()) {
                    case 0:
                        val = QString::fromStdString(value.value().str());
                        break;
                    case 1:
                        val = value.value().dec();
                        break;
                    case 2:
                        val = value.value().fl();
                        break;
                    case 3:
                        QJsonArray array;
                        for(const float &value : value.value().float_vec())
                            array.push_back(value);
                        val = array;                        
                        break;
                }
                content["type"] = QString::number(value.value()._d());
                content["value"] = val;
                lattrsObject[QString::fromStdString(key)] = content;
            }
            link["linkAttribute"] = lattrsObject;
            nodeLinksArray.push_back(link);
        }
        symbol["links"] = nodeLinksArray;
        symbolsMap[QString::number(node.id())] = symbol;
    }
    dsrObject["symbol"] = symbolsMap;

    QJsonObject jsonObject;
    jsonObject["DSRModel"] = dsrObject;
    //writable data
    QJsonDocument jsonDoc(jsonObject);
    QString strJson(jsonDoc.toJson(QJsonDocument::Compact));
    //write to file
    std::ofstream outfile;
    outfile.open(json_file_path, std::ios_base::out | std::ios_base::trunc);
    outfile << strJson.toStdString();
    outfile.close();
    auto now_c = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::cout << __FILE__ << " " << __FUNCTION__ << "File: " << json_file_path << " written to disk at " << now_c
              << std::endl;
}
