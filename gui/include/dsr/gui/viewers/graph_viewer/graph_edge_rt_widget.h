//
// Created by robolab on 2/6/21.
//

#ifndef DSR_GRAPHEDGERTWIDGET_H
#define DSR_GRAPHEDGERTWIDGET_H

#include <QLabel>
#include <Eigen/Geometry> 
#include "graph_edge_rt_widget_UI.h"

static const int precision = 3;

class GraphEdgeRTWidget : public  QWidget
{
    Q_OBJECT
public:
    GraphEdgeRTWidget(std::shared_ptr<DSR::DSRGraph> graph_, const DSR::IDType &from_, const DSR::IDType &to_, const std::string &label_) :
            graph(std::move(graph_)), from(from_), to(to_), edge_type(label_)
    {

        ui.setupUi(this);

        qRegisterMetaType<std::int32_t>("std::int32_t");
        qRegisterMetaType<std::uint32_t>("std::uint32_t");
        qRegisterMetaType<std::uint64_t>("std::uint64_t");
        qRegisterMetaType<uint64_t>("uint64_t");
        qRegisterMetaType<std::string>("std::string");
        qRegisterMetaType<std::map<std::string, DSR::Attribute>>("Attribs");

        connect(graph.get(), &DSR::DSRGraph::update_edge_signal, this, &GraphEdgeRTWidget::add_or_assign_edge_slot, Qt::QueuedConnection);
        connect(graph.get(), &DSR::DSRGraph::update_edge_attr_signal, this, &GraphEdgeRTWidget::add_or_assign_edge_attr_slot, Qt::QueuedConnection);
        //Inner Api
        inner_eigen = graph->get_inner_eigen_api();

        std::optional<DSR::Node> from_node = graph->get_node(from);
        std::optional<DSR::Node> to_node = graph->get_node(to);
        std::optional<DSR::Edge> edge = graph->get_edge(from, to, edge_type);
        if (edge.has_value() and from_node.has_value() and to_node.has_value())
        {
            const auto is_robot_node = [](const DSR::Node &node)
            {
                return node.type() == "robot" || node.name() == "robot";
            };

            QString display_from_type;
            QString display_to_type;

            // Prefer showing the robot pose relative to the other endpoint, regardless
            // of whether the RT edge is parent->robot or robot->parent.
            if (is_robot_node(from_node.value()))
            {
                from_string = from_node.value().name();
                to_string = to_node.value().name();
                display_from_type = QString::fromStdString(from_node.value().type());
                display_to_type = QString::fromStdString(to_node.value().type());
            }
            else if (is_robot_node(to_node.value()))
            {
                from_string = to_node.value().name();
                to_string = from_node.value().name();
                display_from_type = QString::fromStdString(to_node.value().type());
                display_to_type = QString::fromStdString(from_node.value().type());
            }
            else
            {
                // Preserve the previous child-in-parent default for non-robot RT edges.
                from_string = to_node.value().name();
                to_string = from_node.value().name();
                display_from_type = QString::fromStdString(to_node.value().type());
                display_to_type = QString::fromStdString(from_node.value().type());
            }

            setWindowTitle(QString::fromStdString(edge_type) + ": "
                           + QString::fromStdString(from_string) + "(" + display_from_type + ") in "
                           + QString::fromStdString(to_string) + "(" + display_to_type + ")");
            connect(ui.comboBox_reference, SIGNAL(currentTextChanged(QString)), this, SLOT(update_combo(QString)));

            //TODO: temporary added to check yolo pose estimation
            // qDebug()<<__FUNCTION__ <<"Yolo pose";
            if (label_ == "looking-at")
            {
                int currentRowCount = this->ui.tableWidget_Robot->rowCount();
                this->ui.tableWidget_Robot->setRowCount(currentRowCount + 2); 

                // Agregar datos para "Looking pos"
                this->ui.tableWidget_Robot->setItem(currentRowCount, 0, new QTableWidgetItem("Looking pos X"));
                this->ui.tableWidget_Robot->setItem(currentRowCount, 1, new QTableWidgetItem("Looking pos Y"));
                this->ui.tableWidget_Robot->setItem(currentRowCount, 2, new QTableWidgetItem("Looking pos Z"));

                // Agregar datos para "Looking rot"
                this->ui.tableWidget_Robot->setItem(currentRowCount + 1, 0, new QTableWidgetItem("Looking rot X"));
                this->ui.tableWidget_Robot->setItem(currentRowCount + 1, 1, new QTableWidgetItem("Looking rot Y"));
                this->ui.tableWidget_Robot->setItem(currentRowCount + 1, 2, new QTableWidgetItem("Looking rot Z"));
            }
            update_combo(ui.comboBox_reference->currentText());
            show();
        }
    };
    void generate_node_transform_list(const std::string& to, const std::string& from)
    {
        transform_set.clear();
        transform_set.insert(from);
        auto node = graph->get_node(from);
        try{
            std::optional<Node> parent_node;
            do
            {
                parent_node = graph->get_parent_node(node.value());
                if (parent_node.has_value())
                {
                    transform_set.insert(parent_node.value().name());
                    node = parent_node;
                }
            }while(node.value().type() != "root" and node.value().name() != to);
        }catch(...){}
    };
    void closeEvent (QCloseEvent *event) override
    {
        //graph.reset();
        disconnect(graph.get(), 0, this, 0);
    };
public slots:
    void update_combo(const QString& combo_text)
    {
        this->reference = to_string;
        if (combo_text == "root")
        {
            auto root_opt = graph->get_node_root();
            if(root_opt.has_value())
                this->reference = graph->get_node_root().value().name();
        }
        generate_node_transform_list(this->reference, this->from_string);
        add_or_assign_edge_slot(from, to, edge_type);
    };
    void update_values()
    {
        std::optional<Mat::Vector6d> transform;

        // For the default popup view, show the clicked RT edge values directly.
        // This avoids relying on InnerEigen's cached global transforms for a case
        // where the user expects the edge's live stored pose.
        if (this->edge_type == "RT" && this->reference == this->to_string)
        {
            auto from_node = graph->get_node(from);
            auto to_node = graph->get_node(to);
            auto edge = graph->get_edge(from, to, edge_type);
            auto rt_api = graph->get_rt_api();

            if (from_node.has_value() && to_node.has_value() && edge.has_value() && rt_api)
            {
                if (auto rtmat_opt = rt_api->get_edge_RT_as_rtmat(edge.value(), 0); rtmat_opt.has_value())
                {
                    Mat::RTMat rtmat = rtmat_opt.value();

                    const bool raw_matches_requested =
                        (this->reference == from_node->name() && this->from_string == to_node->name());
                    const bool inverse_matches_requested =
                        (this->reference == to_node->name() && this->from_string == from_node->name());

                    if (inverse_matches_requested)
                        rtmat = rtmat.inverse();

                    if (raw_matches_requested || inverse_matches_requested)
                    {
                        Mat::Vector6d values;
                        const auto angles = rtmat.rotation().eulerAngles(0, 1, 2);
                        values << rtmat.translation().x(), rtmat.translation().y(), rtmat.translation().z(),
                                  angles.x(), angles.y(), angles.z();
                        transform = values;
                    }
                }
            }
        }

        if (!transform.has_value())
            transform = inner_eigen->transform_axis(this->reference, this->from_string, 0, this->edge_type);

        if (transform.has_value())
        {
    // std::vector<std::vector<std::string>> attrib_names = {{"X", "Y", "Z"}, {"RX (rad)", "RY (rad)", "RZ (rad)"}, {"RX (deg)", "RY (deg)", "RZ (deg)"} };

            double angles[3];

            for(int pos = 0;pos < 3;pos++)
            {
                //Get position
                this->ui.tableWidget_Robot->item(0, pos)->setText(QString::number(transform.value()[pos], 'g', precision));

                //Get angles
                angles[pos] = transform.value()[pos+3];
                this->ui.tableWidget_Robot->item(1, pos)->setText(QString::number(angles[pos], 'g', precision)); //Radians
                this->ui.tableWidget_Robot->item(2, pos)->setText(QString::number(angles[pos] * 180 / M_PI, 'g', precision)); //Degrees
            }

            //Quaternion
            Eigen::Quaterniond quaternion = Eigen::AngleAxisd(angles[0], Eigen::Vector3d::UnitX()) * 
                                            Eigen::AngleAxisd(angles[1], Eigen::Vector3d::UnitY()) * 
                                            Eigen::AngleAxisd(angles[2], Eigen::Vector3d::UnitZ());

            this->ui.tableWidget_Robot->item(3, 0)->setText(QString::number(quaternion.x(), 'g', precision));
            this->ui.tableWidget_Robot->item(3, 1)->setText(QString::number(quaternion.y(), 'g', precision));
            this->ui.tableWidget_Robot->item(3, 2)->setText(QString::number(quaternion.z(), 'g', precision));
            this->ui.tableWidget_Robot->item(3, 3)->setText(QString::number(quaternion.w(), 'g', precision));


        }
        else
            std::cerr<<__FUNCTION__<<"Error retriving edge data"<<std::endl;

    };
    void add_or_assign_edge_slot( std::uint64_t from,  std::uint64_t to, const std::string& edge_type)
    {
        if (edge_type==this->edge_type)
        {
            //pose values
            if(edge_type == "RT" || edge_type == "looking-at" || edge_type == "VRT")
            {
                std::optional<Node> from_node = graph->get_node(from);
                std::optional<Node> to_node = graph->get_node(to);
                if(from_node.has_value() and to_node.has_value())
                {
                    std::string from_str = from_node.value().name();
                    std::string to_str = to_node.value().name();

                    //check if any node is involved in actual reference transform
                    if(transform_set.find(from_str)!= transform_set.end() or transform_set.find(to_str) != transform_set.end())
                        update_values();
                }
            }
            // velocity and covariance matrix
            if (edge_type == "RT" || edge_type == "VRT"){

                std::optional<DSR::Edge> edge = graph->get_edge(from, to, edge_type);
                if(edge.has_value())
                {
                    std::optional<const std::vector<float>>rotation_vel = graph->get_attrib_by_name<rt_rotation_euler_xyz_velocity_att>(edge.value());
                    std::optional<const std::vector<float>>translation_vel = graph->get_attrib_by_name<rt_translation_velocity_att>(edge.value());
                    std::optional<const std::vector<float>>rotation_acc = graph->get_attrib_by_name<rt_rotation_euler_xyz_acceleration_att>(edge.value());
                    std::optional<const std::vector<float>>translation_acc = graph->get_attrib_by_name<rt_translation_acceleration_att>(edge.value());
                    std::optional<const std::vector<float>>se2_covariance = graph->get_attrib_by_name<rt_se2_covariance_att>(edge.value());
                    std::optional<const std::vector<float>>se2_covariance_velocity = graph->get_attrib_by_name<rt_se2_covariance_velocity_att>(edge.value());
                    std::optional<const std::vector<float>>se2_covariance_acceleration = graph->get_attrib_by_name<rt_se2_covariance_acceleration_att>(edge.value());
                    
                    if(translation_vel.has_value())
                        for(int pos = 0;pos < 3;pos++)
                            this->ui.tableWidget_Robot->item(4, pos)->setText(QString::number(translation_vel.value()[pos], 'g', precision)); //Lineal Vel

                    if(rotation_vel.has_value())
                        for(int pos = 0;pos < 3;pos++)
                            this->ui.tableWidget_Robot->item(5, pos)->setText(QString::number(rotation_vel.value()[pos], 'g', precision)); //Rot Vel
                    
                    if(translation_acc.has_value())
                        for(int pos = 0;pos < 3;pos++)
                            this->ui.tableWidget_Robot->item(6, pos)->setText(QString::number(translation_acc.value()[pos], 'g', precision)); //Lineal ACC
    
                    if(rotation_acc.has_value())
                        for(int pos = 0;pos < 3;pos++)
                            this->ui.tableWidget_Robot->item(7, pos)->setText(QString::number(rotation_acc.value()[pos], 'g', precision)); //Rot ACC

                    if (se2_covariance.has_value())
                        for(int posx = 0; posx < 6; posx++)
                            for(int posy = 0; posy < 6; posy++)
                                this->ui.tableWidget_covariance_pose_matrix->item(posy, posx)->setText(QString::number(se2_covariance.value()[posy*6 + posx], 'g', precision)); //Covariance Matrix

                    if (se2_covariance_velocity.has_value())
                        for(int posx = 0; posx < 6; posx++)
                            for(int posy = 0; posy < 6; posy++)
                                this->ui.tableWidget_covariance_velocity_matrix->item(posy, posx)->setText(QString::number(se2_covariance_velocity.value()[posy*6 + posx], 'g', precision)); //Covariance Matrix

                    if (se2_covariance_acceleration.has_value())
                        for(int posx = 0; posx < 6; posx++)
                            for(int posy = 0; posy < 6; posy++)
                                this->ui.tableWidget_covariance_acceleration_matrix->item(posy, posx)->setText(QString::number(se2_covariance_acceleration.value()[posy*6 + posx], 'g', precision)); //Covariance Matrix
                } 
                else 
                    std::cerr<<__FUNCTION__<<" Error retriving RT data"<<std::endl;
            }
            // if(edge_type == "looking-at")
            // {
            //     std::optional<DSR::Edge> edge = graph->get_edge(from, to, edge_type);
            //     if(edge.has_value())
            //     {
            //         std::optional<const std::vector<float>>rotation = graph->get_attrib_by_name<looking_at_rotation_euler_xyz_att>(edge.value());
            //         std::optional<const std::vector<float>>translation = graph->get_attrib_by_name<looking_at_translation_att>(edge.value());
            //         if(rotation.has_value() and translation.has_value())
            //         {
            //             for(unsigned int pos = 9;pos < 12;pos++)
            //             {
            //                 const double &value = translation.value()[pos-9];
            //                 attrib_widgets[pos]->setText(QString::number(value));
            //             }
            //             for(unsigned int pos = 12;pos < 15;pos++)
            //             {
            //                 const double &value = rotation.value()[pos-12];
            //                 attrib_widgets[pos]->setText(QString::number(value));
            //                 //convert degress
            //                 const double &value_d = value * 180 / M_PI;
            //                 attrib_widgets[3 + pos]->setText(QString::number(value_d));
            //             }
            //             //std::cout<<"print values"<<rotation.value()<<translation.value()<<std::endl;
            //         }
            //     }
            // }
        }
    };
    void add_or_assign_edge_attr_slot(std::uint64_t from,
                                      std::uint64_t to,
                                      const std::string& edge_type,
                                      const std::vector<std::string>& /*att_name*/)
    {
        add_or_assign_edge_slot(from, to, edge_type);
    };

private:
    std::shared_ptr<DSR::DSRGraph> graph;
    std::shared_ptr<DSR::InnerEigenAPI> inner_eigen;
    uint64_t from, to;
    std::string edge_type;
    std::string from_string;
    std::string to_string;
    std::string reference;
    //std::map<int, QLineEdit*> attrib_widgets;
    //std::vector<std::vector<std::string>> attrib_names = {{"X", "Y", "Z"}, {"RX (rad)", "RY (rad)", "RZ (rad)"}, {"RX (deg)", "RY (deg)", "RZ (deg)"} };
    std::set<std::string> transform_set;
    Ui_GraphEdgeRTWidget ui;
};

#endif //DSR_GRAPHEDGERTWIDGET_H
