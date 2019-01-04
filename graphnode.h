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
#include <cppitertools/zip.hpp>
#include <QLabel>
#include "graphviewer.h"

class GraphEdge;
class QGraphicsSceneMouseEvent;

#include "specificworker.h"

class DoLaserStuff : public QGraphicsView
{
  Q_OBJECT
  public:
    DoLaserStuff(std::shared_ptr<DSR::Graph> graph_, DSR::IDType node_id_) : graph(graph_), node_id(node_id_)
    {
      //setWindowFlags(Qt::Widget | Qt::FramelessWindowHint);
      resize(400,400);
      setWindowTitle("Laser");
      scene.setSceneRect(-5000, -100, 10000, 5000);
      setScene(&scene);
      //setViewport(new QGLWidget(QGLFormat(QGL::SampleBuffers)));
	    setRenderHint(QPainter::Antialiasing);
	    fitInView(scene.sceneRect(), Qt::KeepAspectRatio );
      scale(1, -1);
      QObject::connect(graph.get(), &DSR::Graph::NodeAttrsChangedSIGNAL, this, &DoLaserStuff::drawLaserSLOT);                        
      show();
    };
  public slots:
    void drawLaserSLOT(const DSR::IDType &id, const DSR::Attribs &attribs)
    {
      try
      {
        const auto &lDists = graph->getNodeAttribByName<std::vector<float>>(node_id, "laser_data_dists"); 
        const auto &lAngles = graph->getNodeAttribByName<std::vector<float>>(node_id, "laser_data_angles"); 
        QPolygonF polig;
        for(const auto &[dist, angle] : iter::zip(lDists, lAngles))
            polig << QPointF(dist*sin(angle), dist*cos(angle));
        scene.clear();
        QPolygonF robot; robot << QPointF(-200, 0) << QPointF(-100,150) << QPointF(0,200) << QPointF(100,150) << QPointF(200,0);
        scene.addPolygon(robot, QPen(Qt::blue, 8), QBrush(Qt::blue));
        scene.addPolygon(polig, QPen(QColor("LightPink"), 8), QBrush(QColor("LightPink")));  
      }
      catch(const std::exception &e){ std::cout << "Node " << node_id << " not found" << std::endl;};
    };
  private:
    QGraphicsScene scene;
    std::shared_ptr<DSR::Graph> graph;
    DSR::IDType node_id;
};

class DoRGBDStuff : public  QLabel
{
  public:
    DoRGBDStuff(std::shared_ptr<DSR::Graph> graph, DSR::IDType node_id_)
    {
      auto node_id = node_id_;
      //setWindowFlags(Qt::Widget | Qt::FramelessWindowHint);
      //setViewport(new QGLWidget(QGLFormat(QGL::SampleBuffers)));
      resize(640,480);
      setWindowTitle("RGBD");
      setParent(this);
      QObject::connect(graph.get(), &DSR::Graph::NodeAttrsChangedSIGNAL, [&](const DSR::IDType &id, const DSR::Attribs &attrs){ 
                            const auto &lDists = graph->getNodeAttribByName<std::vector<float>>(node_id, "rgbd_data"); 
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
    DoTableStuff(std::shared_ptr<DSR::Graph> graph_, DSR::IDType node_id_) : graph(graph_), node_id(node_id_)
    {
      qRegisterMetaType<std::int32_t>("std::int32_t");
	    qRegisterMetaType<std::string>("std::string");
	    qRegisterMetaType<DSR::Attribs>("DSR::Attribs");

      //setWindowFlags(Qt::Widget | Qt::FramelessWindowHint);
      setColumnCount(2);
      setRowCount(graph->getNodeAttrs(node_id).size() );
      setHorizontalHeaderLabels(QStringList{"Key", "Value"}); 
      int i=0;
      for( auto &[k, v] : graph->getNodeAttrs(node_id) )
      {
        setItem(i, 0, new QTableWidgetItem(QString::fromStdString(k)));
        setItem(i, 1, new QTableWidgetItem(QString::fromStdString(graph->printVisitor(v))));
        i++;
      }
      horizontalHeader()->setStretchLastSection(true);
      resizeRowsToContents();
      resizeColumnsToContents();
      QObject::connect(graph.get(), &DSR::Graph::NodeAttrsChangedSIGNAL, this, &DoTableStuff::drawSLOT);      
      show();
    }
  public slots:
    void drawSLOT(const DSR::IDType &id, const DSR::Attribs &attribs)
    {
      int i= 0; 
      for(auto &[k,v]: attribs)
      {
        setItem(i, 0, new QTableWidgetItem(QString::fromStdString(k)));
        setItem(i, 1, new QTableWidgetItem(QString::fromStdString(graph->printVisitor(v))));
        i++;
      }
    }
  private:
    std::shared_ptr<DSR::Graph> graph;
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
    // void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    // void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;
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
