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

#ifndef GRAPHEDGE_H
#define GRAPHEDGE_H

#include <QGraphicsItem>
#include <QContextMenuEvent>
#include <QTableWidget>
#include <memory>
#include <cppitertools/zip.hpp>
#include <QLabel>
#include "CRDT_graphviewer.h"
#include <qmat/QMatAll>
#include <QHeaderView>
#include <cppitertools/range.hpp>


class DoRTStuff : public  QTableWidget
{
  Q_OBJECT
  public:
    DoRTStuff(std::shared_ptr<CRDT::CRDTGraph> graph_, const CRDT::IDType &from_, const CRDT::IDType &to_, const std::string &label_) :
        graph(graph_), from(from_), to(to_), label(label_)
    {
      qRegisterMetaType<CRDT::IDType>("DSR::IDType");
      qRegisterMetaType<CRDT::Attribs>("DSR::Attribs");
      Node n = graph->get_node(graph->get_name_from_id(from));
      Node n2 = graph->get_node(graph->get_name_from_id(to));

      //setWindowModality(Qt::ApplicationModal);
      setWindowTitle("RT: " + QString::fromStdString(graph->get_node_type(n)) + " to " + QString::fromStdString(graph->get_node_type(n2)));
      setColumnCount(4);
      setRowCount(4);
      setHorizontalHeaderLabels(QStringList{"a", "b", "c", "d"}); 
      setVerticalHeaderLabels(QStringList{"a", "b", "c", "d"}); 
      horizontalHeader()->setStretchLastSection(true);
      resizeRowsToContents();
      resizeColumnsToContents();      
      drawSLOT(from, to);	
      QObject::connect(graph.get(), &CRDT::CRDTGraph::update_edge_signal, this, &DoRTStuff::drawSLOT);
      show();
      std::cout << __FILE__ << " " << __FUNCTION__ << " End ofDoRTStuff Constructor "  << std::endl;
    };
  
  void closeEvent (QCloseEvent *event) override 
  {
    disconnect(graph.get(), 0, this, 0);
    QTableWidget::closeEvent(event);
  };

  public slots:
    void drawSLOT(const std::int32_t &from_, const std::int32_t &to_)
    {
      std::cout << __FILE__ << " " << __FUNCTION__ << std::endl;
      if( from == from_ and to == to_)     //ADD LABEL
        try
        {
          std::string n_from = graph->get_name_from_id(from);
          std::string n_to = graph->get_name_from_id(to);

          EdgeAttribs ea = graph->get_edge(n_from, n_to);

          auto value = ea.attrs().find("RT");
          if (value != ea.attrs().end()) {
              //auto rtvalue = value->second.value();
              auto mat = graph->get_attrib_by_name<RMat::RTMat>(ea, "RT");

              //mat.print("mat");
              for (auto i : iter::range(mat.nRows()))
                  for (auto j : iter::range(mat.nCols()))
                      if (item(i, j) == 0)
                          this->setItem(i, j, new QTableWidgetItem(QString::number(mat(i, j))));
                      else
                          this->item(i, j)->setText(QString::number(mat(i, j)));
          }
        }
        catch (const std::exception &e)
        { std::cout << "Exception: " << e.what() << " Cannot find attribute named RT in edge going " << from << " to " << to << std::endl;}
    }
  private:
    std::shared_ptr<CRDT::CRDTGraph> graph;
    int from, to;
    std::string label;
};



class GraphEdge : public QGraphicsItem, public std::enable_shared_from_this<GraphEdge>
{
	public:
    GraphEdge(GraphNode *sourceNode, GraphNode *destNode, const QString &edge_name);
    GraphNode *sourceNode() const;
    GraphNode *destNode() const;
    void adjust();
    int type() const override { return Type; }
    QString getTag() const { return tag;};

	protected:
    QPainterPath shape() const override;
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
		void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;  
    void keyPressEvent(QKeyEvent *event) override;
	
	private:
		GraphNode *source, *dest;  //CAMBIAR A FROM TO
    qreal arrowSize;
    QPointF sourcePoint;
    QPointF destPoint;
		QString tag;
		QGraphicsTextItem *rt_values = nullptr;
    QTableWidget *label = nullptr;
};


#endif // GRAPHEDGE_H
