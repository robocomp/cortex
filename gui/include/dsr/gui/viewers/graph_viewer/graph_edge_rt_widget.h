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
            QString display_from_type;
            QString display_to_type;

            // RT edges store the child pose in the parent frame, so display the
            // clicked edge using that native child-in-parent orientation.
            from_string = to_node.value().name();
            to_string = from_node.value().name();
            display_from_type = QString::fromStdString(to_node.value().type());
            display_to_type = QString::fromStdString(from_node.value().type());

            setWindowTitle(QString::fromStdString(edge_type) + ": "
                           + QString::fromStdString(from_string) + "(" + display_from_type + ") in "
                           + QString::fromStdString(to_string) + "(" + display_to_type + ")");
            if (label_ == "RT" || label_ == "VRT")
            {
                // Offer the two directions of THIS edge, named by the actual nodes
                // (not the hard-coded room/robot pair). from_string is the child
                // (the RT 'to' node); to_string is the parent (the RT 'from' node).
                child_in_parent_label = from_string + " in " + to_string;  // native RT payload: child pose in parent
                parent_in_child_label = to_string + " in " + from_string;  // inverse
                ui.comboBox_reference->setItemText(0, QString::fromStdString(child_in_parent_label));
                ui.comboBox_reference->setItemText(1, QString::fromStdString(parent_in_child_label));
                ui.comboBox_reference->setCurrentText(QString::fromStdString(child_in_parent_label)); // default = stored payload (matches title)
                ui.comboBox_reference->setEnabled(true);
            }
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
        if (this->edge_type == "RT" || this->edge_type == "VRT")
        {
            this->reference = combo_text.toStdString();
        }
        else if (combo_text == "root")
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

        // RT/VRT popups show the raw clicked edge payload: child pose in parent.
        if (this->edge_type == "RT" || this->edge_type == "VRT")
        {
            auto edge = graph->get_edge(from, to, edge_type);
            auto rt_api = graph->get_rt_api();

            if (edge.has_value() && rt_api)
            {
                if (auto rtmat_opt = rt_api->get_edge_RT_as_rtmat(edge.value(), 0); rtmat_opt.has_value())
                {
                    // Native payload is child-in-parent; invert only when the user picked parent-in-child.
                    const auto rtmat = (this->reference == parent_in_child_label) ? rtmat_opt->inverse() : *rtmat_opt;
                    Mat::Vector6d values;
                    const auto euler_angles = rtmat.rotation().eulerAngles(0, 1, 2);
                    values << rtmat.translation().x(), rtmat.translation().y(), rtmat.translation().z(),
                              euler_angles.x(), euler_angles.y(), euler_angles.z();
                    transform = values;
                }
            }
        }

        if (not transform.has_value())
            transform = inner_eigen->transform_axis(this->reference, this->from_string, 0, this->edge_type);

        if (transform.has_value())
        {
    // std::vector<std::vector<std::string>> attrib_names = {{"X", "Y", "Z"}, {"RX (rad)", "RY (rad)", "RZ (rad)"}, {"RX (deg)", "RY (deg)", "RZ (deg)"} };

            double angles[3];
            auto ensure_item = [this](int row, int column) -> QTableWidgetItem *
            {
                if (auto *item = this->ui.tableWidget_Robot->item(row, column); item != nullptr)
                    return item;

                auto *item = new QTableWidgetItem();
                this->ui.tableWidget_Robot->setItem(row, column, item);
                return item;
            };

            for(int pos = 0;pos < 3;pos++)
            {
                //Get position
                ensure_item(0, pos)->setText(QString::number(transform.value()[pos], 'g', precision));

                //Get angles
                angles[pos] = transform.value()[pos+3];
                ensure_item(1, pos)->setText(QString::number(angles[pos], 'g', precision)); //Radians
                ensure_item(2, pos)->setText(QString::number(angles[pos] * 180 / M_PI, 'g', precision)); //Degrees
            }

            // Show the planar yaw explicitly as a dedicated summary column.
            ensure_item(1, 3)->setText(QString::number(angles[2], 'g', precision));
            ensure_item(2, 3)->setText(QString::number(angles[2] * 180 / M_PI, 'g', precision));

            //Quaternion
            Eigen::Quaterniond quaternion = Eigen::AngleAxisd(angles[0], Eigen::Vector3d::UnitX()) * 
                                            Eigen::AngleAxisd(angles[1], Eigen::Vector3d::UnitY()) * 
                                            Eigen::AngleAxisd(angles[2], Eigen::Vector3d::UnitZ());

            ensure_item(3, 0)->setText(QString::number(quaternion.x(), 'g', precision));
            ensure_item(3, 1)->setText(QString::number(quaternion.y(), 'g', precision));
            ensure_item(3, 2)->setText(QString::number(quaternion.z(), 'g', precision));
            ensure_item(3, 3)->setText(QString::number(quaternion.w(), 'g', precision));


        }
        else
            std::cerr<<__FUNCTION__<<"Error retriving edge data"<<std::endl;

    };
    void add_or_assign_edge_slot( std::uint64_t from,  std::uint64_t to, const std::string& edge_type)
    {
        if (edge_type==this->edge_type)
        {
            if (edge_type == "RT" || edge_type == "VRT")
                update_values();

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
                    {
                        inner_eigen->add_or_assign_edge_slot(from, to, edge_type);
                        update_values();
                    }
                }
            }
            // velocity and covariance matrix
            // Always read from the popup's own edge (this->from / this->to / this->edge_type),
            // never from the signal parameters: an unrelated RT update under the same parent
            // would otherwise inject foreign velocity/covariance values into this popup.
            if (this->edge_type == "RT" || this->edge_type == "VRT"){

                std::optional<DSR::Edge> edge = graph->get_edge(this->from, this->to, this->edge_type);
                if(edge.has_value())
                {
                    std::optional<const std::vector<float>>rotation_vel = graph->get_attrib_by_name<rt_rotation_euler_xyz_velocity_att>(edge.value());
                    std::optional<const std::vector<float>>translation_vel = graph->get_attrib_by_name<rt_translation_velocity_att>(edge.value());
                    std::optional<const std::vector<float>>rotation_acc = graph->get_attrib_by_name<rt_rotation_euler_xyz_acceleration_att>(edge.value());
                    std::optional<const std::vector<float>>translation_acc = graph->get_attrib_by_name<rt_translation_acceleration_att>(edge.value());
                    std::optional<const std::vector<float>>se2_covariance = graph->get_attrib_by_name<rt_covariance_att>(edge.value());
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
        if ((edge_type == "RT" || edge_type == "VRT") && from == this->from && to == this->to)
            update_values();

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
    std::string child_in_parent_label;   // "<child> in <parent>" — native RT payload direction
    std::string parent_in_child_label;   // "<parent> in <child>" — inverse direction
    //std::map<int, QLineEdit*> attrib_widgets;
    //std::vector<std::vector<std::string>> attrib_names = {{"X", "Y", "Z"}, {"RX (rad)", "RY (rad)", "RZ (rad)"}, {"RX (deg)", "RY (deg)", "RZ (deg)"} };
    std::set<std::string> transform_set;
    Ui_GraphEdgeRTWidget ui;
};

#endif //DSR_GRAPHEDGERTWIDGET_H
