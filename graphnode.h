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

class GraphEdge;
class GraphViewer;
class QGraphicsSceneMouseEvent;
#include "specificworker.h"

class DoLaserStuff : public QGraphicsView
{
  public:
    DoLaserStuff(const GraphViewer *graph_viewer, IDType node_id_)
    {
      auto node_id = node_id_;
      //setWindowFlags(Qt::Widget | Qt::FramelessWindowHint);
      resize(400,400);
      setWindowTitle("Laser");
      scene.setSceneRect(-5000, -100, 10000, 5000);
      setScene(&scene);
      //setViewport(new QGLWidget(QGLFormat(QGL::SampleBuffers)));
	    setRenderHint(QPainter::Antialiasing);
	    fitInView(scene.sceneRect(), Qt::KeepAspectRatio );
      scale(1, -1);
      const auto &g = graph_viewer->worker->graph;
      QObject::connect(g.get(), &DSR::Graph::NodeAttrsChangedSIGNAL, [g, node_id, this](const DSR::Attribs &attrs){ 
                          const auto &lDists = g->getNodeAttribByName<std::vector<float>>(node_id, "laser_data_dists"); 
                          const auto &lAngles = g->getNodeAttribByName<std::vector<float>>(node_id, "laser_data_angles"); 
                          QPolygonF polig;
                          for(const auto &[dist, angle] : iter::zip(lDists, lAngles))
                              polig << QPointF(dist*sin(angle), dist*cos(angle));
                          scene.clear();
                          QPolygonF robot; robot << QPointF(-200, 0) << QPointF(-100,150) << QPointF(0,200) << QPointF(100,150) << QPointF(200,0);
                          scene.addPolygon(robot, QPen(Qt::blue, 8), QBrush(Qt::blue));
                          scene.addPolygon(polig, QPen(Qt::red, 8));                          
                          });
      show();
    };
  private:
    QGraphicsScene scene;
};

class DoRGBDStuff : public  QLabel
{
  public:
    DoRGBDStuff(const GraphViewer *graph_viewer, IDType node_id_)
    {
      auto node_id = node_id_;
      //setWindowFlags(Qt::Widget | Qt::FramelessWindowHint);
      resize(640,480);
      setWindowTitle("RGBD");
      //setViewport(new QGLWidget(QGLFormat(QGL::SampleBuffers)));
	    const auto &g = graph_viewer->worker->graph;
      setParent(this);
      QObject::connect(g.get(), &DSR::Graph::NodeAttrsChangedSIGNAL, [&](const DSR::Attribs &attrs){ 
                            const auto &lDists = g->getNodeAttribByName<std::vector<float>>(node_id, "rgbd_data"); 
                            //label.setPixmap(QImage());                          
                          });
      show();
    };
  private:
    //QLabel label;
};

class DoTableStuff : public  QTableWidget
{
  public:
    DoTableStuff(const GraphViewer *graph_viewer, IDType node_id_)
    {
      auto node_id = node_id_;
      //setWindowFlags(Qt::Widget | Qt::FramelessWindowHint);
      setColumnCount(2);
      auto g = graph_viewer->worker->graph;
      setRowCount(g->getNodeAttrs(node_id).size() );
      setHorizontalHeaderLabels(QStringList{"Key", "Value"}); 
      int i=0;
      for( auto &[k, v] : g->getNodeAttrs(node_id) )
      {
        setItem(i, 0, new QTableWidgetItem(QString::fromStdString(k)));
        setItem(i, 1, new QTableWidgetItem(QString::fromStdString(g->printVisitor(v))));
        i++;
      }
      horizontalHeader()->setStretchLastSection(true);
      resizeRowsToContents();
      resizeColumnsToContents();
      QObject::connect(graph_viewer, &GraphViewer::closeWindowSIGNAL, &QTableWidget::close);
      QObject::connect(g.get(), &DSR::Graph::NodeAttrsChangedSIGNAL, [&](const DSR::Attribs &attrs)
                    { 
                      int i= 0; 
                      for(auto &[k,v]: attrs)
                      {
                          setItem(i, 0, new QTableWidgetItem(QString::fromStdString(k)));
                          setItem(i, 1, new QTableWidgetItem(QString::fromStdString(g->printVisitor(v))));
                          i++;
                      }});
      show();
  };
   private:
};

class GraphNode : public QObject, public QGraphicsItem
{
  Q_OBJECT
	public:
    GraphNode(GraphViewer *graph_viewer);
    
    std::int32_t id_in_graph;
    GraphViewer *graph;
    QList<GraphEdge *> edgeList;
    
    void addEdge(GraphEdge *edge);
    QList<GraphEdge *> edges() const;
    void calculateForces();
    bool advancePosition();
		void setTag(const QString &tag_);
    QString getTag() const { return tag->text();};
    void setType(const std::string &type_);
    std::string getType() const { return type;};
    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
		void setColor(const QString &plain);

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
    //QTableWidget *label = nullptr;
    //DoLaserStuff *do_stuff;
};

#endif // GRAPHNODE_H
