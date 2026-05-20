//
// Created by robolab on 2/6/21.
//

#ifndef DSR_GRAPHEDGERTWIDGET_H
#define DSR_GRAPHEDGERTWIDGET_H

#include <QLabel>
#include <Eigen/Geometry> 
#include <chrono>
#include <iostream>
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
                ui.comboBox_reference->setItemText(0, "room_in_robot");
                ui.comboBox_reference->setItemText(1, "robot_in_room");
                ui.comboBox_reference->setCurrentText("robot_in_room");
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
                {
                    static auto last_rt_graph_trace = std::chrono::steady_clock::now() - std::chrono::seconds(1);
                    const auto now_rt_graph_trace = std::chrono::steady_clock::now();
                    if (now_rt_graph_trace - last_rt_graph_trace >= std::chrono::seconds(1))
                    {
                        auto translation_pack = graph->get_attrib_by_name<rt_translation_att>(edge.value());
                        auto rotation_pack = graph->get_attrib_by_name<rt_rotation_euler_xyz_att>(edge.value());
                        auto timestamps = graph->get_attrib_by_name<rt_timestamps_att>(edge.value());
                        auto head_index = graph->get_attrib_by_name<rt_head_index_att>(edge.value());

                        if (translation_pack.has_value() && rotation_pack.has_value())
                        {
                            const auto &t = translation_pack.value().get();
                            const auto &r = rotation_pack.value().get();
                            std::size_t selected_block = 0;
                            if (head_index.has_value() && t.size() >= 3)
                                selected_block = (head_index.value() > 0) ? static_cast<std::size_t>(head_index.value() - 3) : t.size() - 3;

                            std::cerr << "[RTTrace][WidgetRaw] edge=" << this->edge_type
                                      << " ref=" << this->reference
                                      << " orig=" << this->from_string
                                      << " head=" << head_index.value_or(-1)
                                      << " block=" << selected_block
                                      << " trans_pack=[";
                            for (std::size_t i = 0; i < t.size(); ++i)
                            {
                                if (i > 0) std::cerr << ",";
                                std::cerr << t[i];
                            }
                            std::cerr << "] rot_pack=[";
                            for (std::size_t i = 0; i < r.size(); ++i)
                            {
                                if (i > 0) std::cerr << ",";
                                std::cerr << r[i];
                            }
                            std::cerr << "] timestamps=[";
                            if (timestamps.has_value())
                            {
                                const auto &ts = timestamps.value().get();
                                for (std::size_t i = 0; i < ts.size(); ++i)
                                {
                                    if (i > 0) std::cerr << ",";
                                    std::cerr << ts[i];
                                }
                            }
                            std::cerr << "] selected_trans=(";
                            if (selected_block + 2 < t.size())
                                std::cerr << t[selected_block] << "," << t[selected_block + 1] << "," << t[selected_block + 2];
                            std::cerr << ") selected_rot=(";
                            if (selected_block + 2 < r.size())
                                std::cerr << r[selected_block] << "," << r[selected_block + 1] << "," << r[selected_block + 2];
                            std::cerr << ")\n";
                        }
                        last_rt_graph_trace = now_rt_graph_trace;
                    }
                }

                if (auto rtmat_opt = rt_api->get_edge_RT_as_rtmat(edge.value(), 0); rtmat_opt.has_value())
                {
                    const auto rtmat = (this->reference == "robot_in_room") ? rtmat_opt->inverse() : *rtmat_opt;
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
            {
                static auto last_rt_trace = std::chrono::steady_clock::now() - std::chrono::seconds(1);
                const auto now_rt_trace = std::chrono::steady_clock::now();
                if (now_rt_trace - last_rt_trace >= std::chrono::seconds(1))
                {
                    std::cerr << "[RTTrace][Viewer] edge=" << this->edge_type
                              << " ref=" << this->reference
                              << " orig=" << this->from_string
                              << " pose=(" << transform.value()[0]
                              << "," << transform.value()[1]
                              << "," << transform.value()[2]
                              << ") rot=(" << transform.value()[3]
                              << "," << transform.value()[4]
                              << "," << transform.value()[5]
                              << ")\n";
                    last_rt_trace = now_rt_trace;
                }
            }

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
                    {
                        inner_eigen->add_or_assign_edge_slot(from, to, edge_type);

                        static auto last_signal_trace = std::chrono::steady_clock::now() - std::chrono::seconds(1);
                        const auto now_signal_trace = std::chrono::steady_clock::now();
                        if (now_signal_trace - last_signal_trace >= std::chrono::seconds(1))
                        {
                            std::cerr << "[RTTrace][ViewerSignal] edge=" << edge_type
                                      << " from=" << from_str
                                      << " to=" << to_str
                                      << " ref=" << this->reference
                                      << " orig=" << this->from_string
                                      << "\n";
                            last_signal_trace = now_signal_trace;
                        }
                        update_values();
                    }
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
