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

#ifndef GRAPHNODE_H
#define GRAPHNODE_H

#include <QGraphicsItem>
#include <QTableWidget>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QStyleOption>
#include <QDebug>
#include <QDialog>
#include <QHeaderView>
#include <QLabel>

#include <cppitertools/zip.hpp>

#include "CRDT.h"
#include "CRDT_graphedge.h"
#include "CRDT_graphviewer.h"



class GraphEdge;
class QGraphicsSceneMouseEvent;

#include "specificworker.h"

class DoLaserStuff : public QGraphicsView
{
  Q_OBJECT
  public:
    DoLaserStuff(std::shared_ptr<CRDT::CRDTGraph> graph_, std::int32_t node_id_) : graph(graph_), node_id(node_id_)
    {
      std::cout << __FUNCTION__ << std::endl;
      //setWindowFlags(Qt::Widget | Qt::FramelessWindowHint);
      resize(400,400);
      setWindowTitle("Laser");
      scene.setSceneRect(-5000, -100, 10000, 5000);
      setScene(&scene);
      //setViewport(new QGLWidget(QGLFormat(QGL::SampleBuffers)));
      setRenderHint(QPainter::Antialiasing);
      fitInView(scene.sceneRect(), Qt::KeepAspectRatio );
      scale(1, -1);
      //drawLaserSLOT(node_id_, );
      //QObject::connect(graph.get(), &CRDT::CRDTGraph::update_attrs_signal, this, &DoLaserStuff::drawLaserSLOT);
      QObject::connect(graph.get(), &CRDT::CRDTGraph::update_node_signal, this, &DoLaserStuff::drawLaserSLOT);
      show();
    };

  void closeEvent (QCloseEvent *event) override 
  {
    disconnect(graph.get(), 0, this, 0);
  };

  public slots:
    //void drawLaserSLOT(const std::int32_t &id, const Attribs &attribs)
    void drawLaserSLOT( int id, const std::string &type )
    {
      if( type != "laser")
        return;
      try
      {
        std::cout << __FUNCTION__ <<"-> Node: "<<id<< std::endl;
        const vector<float> lAngles = graph->get_node_attrib_by_name<vector<float>>(id, "laser_data_angles");
        const vector<float> lDists = graph->get_node_attrib_by_name<vector<float>>(id, "laser_data_dists");

        QPolygonF polig;
        for(const auto v : lAngles)
            std::cout <<v;
          cout<<std::endl;
          for(const auto v : lDists)
              std::cout << v;
        cout<<std::endl;
        for(const auto &[dist, angle] : iter::zip(lDists, lAngles))
        {
          std::cout << dist<< ","<<angle<<std::endl;
          polig << QPointF(dist*sin(angle), dist*cos(angle));
        }
        scene.clear();
        QPolygonF robot; robot << QPointF(-200, 0) << QPointF(-100,150) << QPointF(0,200) << QPointF(100,150) << QPointF(200,0);
        scene.addPolygon(robot, QPen(Qt::blue, 8), QBrush(Qt::blue));
        scene.addPolygon(polig, QPen(QColor("LightPink"), 8), QBrush(QColor("LightPink")));
      }
      catch(const std::exception &e){ std::cout << "Node " << node_id << " problem. "<<e.what() << std::endl;};
    };
  private:
    QGraphicsScene scene;
    std::shared_ptr<CRDT::CRDTGraph> graph;
    std::int32_t node_id;
};

class DoRGBDStuff : public  QLabel
{
  public:
    DoRGBDStuff(std::shared_ptr<CRDT::CRDTGraph> graph, DSR::IDType node_id_)
    {
      auto node_id = node_id_;
      //setWindowFlags(Qt::Widget | Qt::FramelessWindowHint);
      //setViewport(new QGLWidget(QGLFormat(QGL::SampleBuffers)));
      resize(640,480);
      setWindowTitle("RGBD");
      setParent(this);
      QObject::connect(graph.get(), &CRDT::CRDTGraph::update_attrs_signal, [&](const std::int32_t &id, const std::vector<AttribValue> &attrs){
                            const auto &lDists = graph->get_node_attrib_by_name<std::vector<float>>(node_id, "rgbd_data");
                            //label.setPixmap(QImage());                          
                          });
      show();
    };
  private:
    QLabel label;
};

class DoTableStuff : public  QTableWidget
{
  Q_OBJECT
  public:
    DoTableStuff(std::shared_ptr<CRDT::CRDTGraph> graph_, DSR::IDType node_id_) : graph(graph_), node_id(node_id_)
    {
      qRegisterMetaType<std::int32_t>("std::int32_t");
      qRegisterMetaType<std::string>("std::string");
      qRegisterMetaType<map<string, AttribValue>>("Attribs");

      //setWindowFlags(Qt::Widget | Qt::FramelessWindowHint);
      setWindowTitle("Node " + QString::fromStdString(graph->get_node_type(node_id)) + " [" + QString::number(node_id) + "]");
      setColumnCount(2);
      setRowCount(graph->get_node_attribs_crdt(node_id).size() );
      setHorizontalHeaderLabels(QStringList{"Key", "Value"}); 
      int i=0;
      for( auto &v : graph->get_node_attribs_crdt(node_id) )
      {
        setItem(i, 0, new QTableWidgetItem(QString::fromStdString(v.key())));
        setItem(i, 1, new QTableWidgetItem(QString::fromStdString(v.value())));
        i++;
      }
      horizontalHeader()->setStretchLastSection(true);
      resizeRowsToContents();
      resizeColumnsToContents();
      QObject::connect(graph.get(), &CRDT::CRDTGraph::update_attrs_signal, this, &DoTableStuff::drawSLOT);
      show();
    };
    
  public slots:
    void drawSLOT(const std::int32_t &id, const std::vector<AttribValue> &attribs)
    {
      int i= 0; 
      for(auto &v: attribs)
      {
        setItem(i, 0, new QTableWidgetItem(QString::fromStdString(v.key())));   //CHANGE TO SET
        setItem(i, 1, new QTableWidgetItem(QString::fromStdString((v.value()))));
        i++;
      }
    }
  private:
    std::shared_ptr<CRDT::CRDTGraph> graph;
    std::int32_t node_id;
};

class GraphNode : public QObject, public QGraphicsItem
{
  Q_OBJECT
	public:
    GraphNode(std::shared_ptr<DSR::GraphViewer> graph_viewer_);
    
    std::int32_t id_in_graph;
    QList<GraphEdge *> edgeList;
    
    void addEdge(GraphEdge *edge);
    QList<GraphEdge *> edges() const;
    void calculateForces();
    bool advancePosition();
		void setTag(const std::string &tag_);
    std::string getTag() const { return tag->text().toStdString();};
    std::string getColor() const { return plain_color.toStdString(); };
    void setType(const std::string &type_);
    std::string getType() const { return type;};
    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
    void setColor(const std::string &plain);
    std::shared_ptr<DSR::GraphViewer> getGraphViewer() const { return graph_viewer;};

	protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;  
    void keyPressEvent(QKeyEvent *event) override;

  public slots:
    void NodeAttrsChangedSLOT(const DSR::IDType &node, const DSR::Attribs&);

	private:
    QPointF newPos;
    QGraphicsSimpleTextItem *tag;
    QString dark_color = "darkyello", plain_color = "yellow";
    std::string type;
    std::shared_ptr<DSR::GraphViewer> graph_viewer;
};

#endif // GRAPHNODE_H


 // for( const auto &[k,v] : sample.getValue())
            //     std::cout << "Received: node " << k << " with laser_data " << v.attrs.at("laser_data_dists") << " from " << sample.getKey() << std::endl;
            //std::cout << "received: " << sample.getValue().at(134).attrs.at("laser_data_dists") << std::endl;
            //std::cout << "--------------------" << std::endl;
