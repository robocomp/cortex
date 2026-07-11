//
// Created by juancarlos on 31/7/20.
//

/*
 * Design note for RT edge covariance support.
 *
 * When RT edge covariance is represented as a full Gaussian on SE(3), the
 * agreed convention is a 6x6 covariance defined on a local se(3)
 * perturbation, ordered as [tx, ty, tz, rx, ry, rz]. The rotational block is
 * expressed as a small-angle perturbation. This convention is intended to be
 * shared by future uncertainty-aware transform APIs so that point, planar
 * pose, and full 6D pose propagation can all use the same edge-level support.
 */

#ifndef DSR_ATTR_NAME_H
#define DSR_ATTR_NAME_H

#include <typeindex>
#include <tuple>
#include <cstdint>
#include <string>
#include <vector>
#include <type_traits>
#include <functional>
#include <unordered_map>
#include <any>
#include <cmath>
#include <memory>
#include <dsr/core/traits.h>
#include <dsr/core/types/crdt_types.h>
#include <dsr/core/types/user_types.h>
#include "type_checker.h"


// Attributes
//Define el tipo utilizado para validar los tipos de atributos durante la compilación
template<const std::string_view& n, AttributeType Tn>
struct Attr {
    static constexpr bool attr_type = std::bool_constant<allowed_types<unwrap_reference_wrapper_t<Tn>>>();
    static constexpr std::string_view attr_name = std::string_view(n);
    static Tn type;
};

template<typename name, class Ta>
static constexpr bool valid_type ()
{
    if constexpr(is_reference_wrapper<decltype(name::type)>::value) {
        using ref_type = typename decltype(name::type)::type;
        using Selected_Type = std::remove_reference_t<std::remove_cv_t<ref_type>>;
        return std::is_same_v<Selected_Type, std::remove_cv_t<std::remove_reference_t<Ta>>>;
    } else {
        using Selected_Type = std::remove_reference_t<std::remove_cv_t<decltype(name::type)>>;
        return std::is_same_v<Selected_Type, std::remove_cv_t<std::remove_reference_t<Ta>>>;
    }
}

template<typename tp>
static constexpr auto reg_fn = []() -> auto
            {
                if constexpr (is_reference_wrapper<tp>::value) {
                    using tp_c = std::remove_const_t<typename tp::type>;
                    return tp_c();
                } else {
                    static_assert(std::is_constructible_v<tp>, "tp is not constructible without arguments, register your type manually");
                    return tp();
                }
            };



#define REGISTER_FN(x, it, stream)  \
                            [[maybe_unused]] inline bool x ##_b =  attribute_types::register_type( x##_str, reg_fn<it>(), stream);     \
                            \


#define REGISTER_TYPE(x, ot, stream) \
                            static constexpr auto    x ##_str = std::string_view(#x ); \
                            using x##_att = Attr< x##_str, ot>;                        \
                            REGISTER_FN(x, ot, stream) \
                            \


#define REGISTER_TYPE_DEPRECATED(x, ot, stream, msg) \
                            static constexpr auto    x ##_str = std::string_view(#x ); \
                            using x##_att [[deprecated(msg)]] = Attr< x##_str, ot>;     \
                            REGISTER_FN(x, ot, stream) \
                            \


#define COMMA_TEMPLATE() ,


inline std::unordered_map<std::string_view, std::function<bool(const std::any&)>> attribute_types::map_fn_;
inline std::vector<std::unique_ptr<std::string>> attribute_types::static_duration_str;

/*
 * Generic
 * */
REGISTER_TYPE(level, int, false)
REGISTER_TYPE(pos_x, float, false)
REGISTER_TYPE(pos_y, float, false)
REGISTER_TYPE(parent, std::uint64_t, false)
REGISTER_TYPE(color, std::reference_wrapper<const std::string>, false)
REGISTER_TYPE(texture, std::reference_wrapper<const std::string>, false)
// Legacy dimensions were historically stored as integers, commonly interpreted as millimeters.
// Prefer the metric float attributes width_m / height_m / depth_m in new code.
REGISTER_TYPE_DEPRECATED(width, int, false, "Use width_m_att instead; legacy width_att is deprecated.")
REGISTER_TYPE_DEPRECATED(height, int, false, "Use height_m_att instead; legacy height_att is deprecated.")
REGISTER_TYPE_DEPRECATED(depth, int, false, "Use depth_m_att instead; legacy depth_att is deprecated.")
REGISTER_TYPE(width_m, float, false)  //meters
REGISTER_TYPE(height_m, float, false)
REGISTER_TYPE(depth_m, float, false)
REGISTER_TYPE(mass, int, false)
REGISTER_TYPE(scalex, int, false)
REGISTER_TYPE(scaley, int, false)
REGISTER_TYPE(scalez, int, false)
REGISTER_TYPE(path, std::reference_wrapper<const std::string>, false)
REGISTER_TYPE(name, std::reference_wrapper<const std::string>, false)
REGISTER_TYPE(active, bool, false)
REGISTER_TYPE(frequency, float, false)  // 


/*
* Edge creation timestamp
*/
REGISTER_TYPE(creation_timestamp, float, true)

/*
 * RT
 * */
REGISTER_TYPE(rt_rotation_euler_xyz, std::reference_wrapper<const std::vector<float>>, true)
REGISTER_TYPE(rt_translation, std::reference_wrapper<const std::vector<float>>, true)
REGISTER_TYPE(rt_quaternion, std::reference_wrapper<const std::vector<float>>, true)
REGISTER_TYPE(rt_translation_velocity, std::reference_wrapper<const std::vector<float>>, true)
REGISTER_TYPE(rt_rotation_euler_xyz_velocity, std::reference_wrapper<const std::vector<float>>, true)
REGISTER_TYPE(rt_translation_acceleration, std::reference_wrapper<const std::vector<float>>, true)
REGISTER_TYPE(rt_rotation_euler_xyz_acceleration, std::reference_wrapper<const std::vector<float>>, true)
REGISTER_TYPE(rt_timestamps, std::reference_wrapper<const std::vector<uint64_t>> , false)
REGISTER_TYPE(rt_covariance, std::reference_wrapper<const std::vector<float>>, true)
REGISTER_TYPE(rt_covariance_velocity, std::reference_wrapper<const std::vector<float>>, true)
REGISTER_TYPE(rt_covariance_acceleration, std::reference_wrapper<const std::vector<float>>, true)
REGISTER_TYPE(rt_head_index, int, false)

/*
 * Legacy names kept for backward compatibility.
 */
REGISTER_TYPE(rt_se2_covariance, std::reference_wrapper<const std::vector<float>>, true)
REGISTER_TYPE(rt_se2_covariance_velocity, std::reference_wrapper<const std::vector<float>>, true)
REGISTER_TYPE(rt_se2_covariance_acceleration, std::reference_wrapper<const std::vector<float>>, true)



/*
 * looking-at
 * */
REGISTER_TYPE(looking_at_rotation_euler_xyz, std::reference_wrapper<const std::vector<float>>, true)
REGISTER_TYPE(looking_at_translation, std::reference_wrapper<const std::vector<float>>, true)
REGISTER_TYPE(looking_at_quaternion, std::reference_wrapper<const std::vector<float>>, true)
/*
 * Laser
 * */
REGISTER_TYPE(laser_angles, std::reference_wrapper<const std::vector<float>>, true)
REGISTER_TYPE(laser_dists, std::reference_wrapper<const std::vector<float>>, true)
REGISTER_TYPE(laser_X, std::reference_wrapper<const std::vector<float>>, true)
REGISTER_TYPE(laser_Y, std::reference_wrapper<const std::vector<float>>, true)
REGISTER_TYPE(laser_Z, std::reference_wrapper<const std::vector<float>>, true)
REGISTER_TYPE(laser_R, std::reference_wrapper<const std::vector<u_int8_t>>, true)
REGISTER_TYPE(laser_G, std::reference_wrapper<const std::vector<u_int8_t>>, true)
REGISTER_TYPE(laser_B, std::reference_wrapper<const std::vector<u_int8_t>>, true)
REGISTER_TYPE(laser_D, std::reference_wrapper<const std::vector<float>>, true)
REGISTER_TYPE(laser_intensities, std::reference_wrapper<const std::vector<u_int8_t>>, true)
REGISTER_TYPE(laser_timestamp, uint64_t, false)
REGISTER_TYPE(laser_i, std::reference_wrapper<const std::vector<u_int8_t>>, true)
REGISTER_TYPE(laser_j, std::reference_wrapper<const std::vector<u_int8_t>>, true)

/*
 * Person
 * */
REGISTER_TYPE(person_social_x_pos, std::reference_wrapper<const std::vector<float>>, true)
REGISTER_TYPE(person_social_y_pos, std::reference_wrapper<const std::vector<float>>, true)
REGISTER_TYPE(person_personal_x_pos, std::reference_wrapper<const std::vector<float>>,true )
REGISTER_TYPE(person_personal_y_pos, std::reference_wrapper<const std::vector<float>>,true)
REGISTER_TYPE(person_sharedWidth, std::reference_wrapper<const std::vector<float>>,true)
REGISTER_TYPE(person_intimate_x_pos, std::reference_wrapper<const std::vector<float>>,true)
REGISTER_TYPE(person_intimate_y_pos, std::reference_wrapper<const std::vector<float>>,true)
REGISTER_TYPE(person_id, std::int32_t, false)
REGISTER_TYPE(distance_to_robot, float, false)
REGISTER_TYPE(lambda_cont, std::int32_t, false)
REGISTER_TYPE(is_ready, bool, false)
REGISTER_TYPE(person_name, std::reference_wrapper<const std::string>, false)
REGISTER_TYPE(person_role, std::reference_wrapper<const std::string>, false)
REGISTER_TYPE(person_age, int, false)
REGISTER_TYPE(person_velocity, std::vector<float>, false)
REGISTER_TYPE(velocity_module, float, false)
REGISTER_TYPE(is_followed, bool, false)
REGISTER_TYPE(is_lost, bool, false)
REGISTER_TYPE(person_image, std::reference_wrapper<const std::vector<uint8_t>>, true)
REGISTER_TYPE(person_image_width, int, true)
REGISTER_TYPE(person_image_height, int, true)
REGISTER_TYPE(person_pixel_x, int, true)
REGISTER_TYPE(person_pixel_y, int, true)

/*
 * Personal Space
 * */
REGISTER_TYPE(ps_social_x_pos, std::reference_wrapper<const std::vector<float>>, true)
REGISTER_TYPE(ps_social_y_pos, std::reference_wrapper<const std::vector<float>>, true)
REGISTER_TYPE(ps_personal_x_pos, std::reference_wrapper<const std::vector<float>>,true )
REGISTER_TYPE(ps_personal_y_pos, std::reference_wrapper<const std::vector<float>>,true)
REGISTER_TYPE(ps_intimate_x_pos, std::reference_wrapper<const std::vector<float>>,true)
REGISTER_TYPE(ps_intimate_y_pos, std::reference_wrapper<const std::vector<float>>,true)

/*
 * Object
 * */
REGISTER_TYPE(obj_id, int, false)
REGISTER_TYPE_DEPRECATED(obj_width, int, false, "Use obj_width_m_att instead; legacy obj_width_att is deprecated.")
REGISTER_TYPE_DEPRECATED(obj_height, int, false, "Use obj_height_m_att instead; legacy obj_height_att is deprecated.")
REGISTER_TYPE_DEPRECATED(obj_depth, int, false, "Use obj_depth_m_att instead; legacy obj_depth_att is deprecated.")
REGISTER_TYPE(obj_width_m, float, false)
REGISTER_TYPE(obj_height_m, float, false)
REGISTER_TYPE(obj_depth_m, float, false)
REGISTER_TYPE(obj_interaction_angle, float, false)
REGISTER_TYPE(obj_interaction_space, float, false)
REGISTER_TYPE(obj_interaction_shape, std::reference_wrapper<const std::string>, false)
REGISTER_TYPE(obj_visible, int, false)
REGISTER_TYPE(projected_bounding_box, std::reference_wrapper<const std::vector<float>>, true)
REGISTER_TYPE(unseen_time, int, false)
REGISTER_TYPE(obj_checked, bool, false)
REGISTER_TYPE(average_size, std::reference_wrapper<const std::string>, false)
REGISTER_TYPE(is_an_obstacle, bool, false)
REGISTER_TYPE(room_id, uint64_t, false)

/*
 * Object affordances
 * 
 **/
REGISTER_TYPE(aff_x_pos, std::reference_wrapper<const std::vector<float>>, true)
REGISTER_TYPE(aff_y_pos, std::reference_wrapper<const std::vector<float>>, true)
REGISTER_TYPE(aff_interacting, bool, true)


/*
 * camera
 * */
REGISTER_TYPE(cam_rgb, std::reference_wrapper<const std::vector<uint8_t>>, true)
REGISTER_TYPE(cam_depth_focalx, int, false)
REGISTER_TYPE(cam_depth_focaly, int, false)
REGISTER_TYPE(cam_depth_alivetime, uint64_t, false)
REGISTER_TYPE(cam_rgb_cameraID, int, false)
REGISTER_TYPE(cam_rgb_focalx, int, false)
REGISTER_TYPE(cam_rgb_focaly, int, false)
REGISTER_TYPE(cam_rgb_alivetime, uint64_t, true)
REGISTER_TYPE(cam_rgb_width, int, false)
REGISTER_TYPE(cam_rgb_height, int, false)
REGISTER_TYPE(cam_rgb_depth, int, true)
REGISTER_TYPE(cam_depth, std::reference_wrapper<const std::vector<uint8_t>>, true)
REGISTER_TYPE(cam_depth_cameraID, int, false)
REGISTER_TYPE(cam_depthFactor, float, false)
REGISTER_TYPE(cam_depth_height, int, false)
REGISTER_TYPE(cam_depth_width, int, false)

/*
 * Robot
 * */

REGISTER_TYPE(viriato_head_pan_tilt_nose_pos_ref, std::reference_wrapper<const std::vector<float>>, true)
REGISTER_TYPE(viriato_head_pan_tilt_nose_speed_ref, std::reference_wrapper<const std::vector<float>>, true)

REGISTER_TYPE(robot_current_advance_speed, float, true) // robot frame m/s
REGISTER_TYPE(robot_current_angular_speed, float, true)     // robot frame rad/s
REGISTER_TYPE(robot_current_side_speed, float, true)    // robot frame m/s
REGISTER_TYPE(robot_current_speed_timestamp, uint64_t, true)
REGISTER_TYPE(robot_local_linear_velocity, std::reference_wrapper<const std::vector<float>>, true)
REGISTER_TYPE(robot_local_angular_velocity, std::reference_wrapper<const std::vector<float>>, true)
REGISTER_TYPE(robot_ref_adv_speed, float, true) // robot frame m/s
REGISTER_TYPE(robot_ref_rot_speed, float, true) // robot frame rad/s
REGISTER_TYPE(robot_ref_side_speed, float, true) // robot frame m/s
REGISTER_TYPE(robot_ref_speed_timestamp, uint64_t, true) //ms
REGISTER_TYPE(robot_target_x, float, true) 
REGISTER_TYPE(robot_target_angle, float, true)
REGISTER_TYPE(robot_occupied, bool, false)
/*
 * Arm
 * */
REGISTER_TYPE(viriato_arm_tip_target, std::reference_wrapper<const std::vector<float>>,  false)

/*
 * Plan
 * */
REGISTER_TYPE(plan, std::reference_wrapper<const std::string>, false)
REGISTER_TYPE(plan_target_node_id, int, false)

/*
 * Mind
 * */
REGISTER_TYPE(current_intention, std::reference_wrapper<const std::string>, false)
REGISTER_TYPE(grid_as_string, std::reference_wrapper<const std::string>, false)
REGISTER_TYPE(arrival_time, float, false)


/*
 * World
 * */
REGISTER_TYPE(OuterRegionLeft, int, false)
REGISTER_TYPE(OuterRegionRight, int, false)
REGISTER_TYPE(OuterRegionBottom, int, false)
REGISTER_TYPE(OuterRegionTop, int, false)
REGISTER_TYPE(world_outline_x, std::reference_wrapper<const std::vector<float>>, false)
REGISTER_TYPE(world_outline_y, std::reference_wrapper<const std::vector<float>>, false)

/*
 * Path to target
 * */
REGISTER_TYPE(path_x_values, std::reference_wrapper<const std::vector<float>>, false)
REGISTER_TYPE(path_y_values, std::reference_wrapper<const std::vector<float>>, false)
REGISTER_TYPE(path_target_x, float, false)
REGISTER_TYPE(path_target_y, float, false)
REGISTER_TYPE(path_is_cyclic, bool, false)

/*
 * Battery
 * */
REGISTER_TYPE(battery_load, float, false);
REGISTER_TYPE(battery_V, float, false);
REGISTER_TYPE(battery_A, float, false);
REGISTER_TYPE(battery_P, float, false);
REGISTER_TYPE(battery_CE, float, false);
REGISTER_TYPE(battery_TTG, float, false);

/*
 * Wifi signal
 * */
REGISTER_TYPE(wifi_signal, int, false);

/*
 * Ultrasound belt
 * */
REGISTER_TYPE(ultrasound_dists, std::reference_wrapper<const std::vector<float>>, false);
REGISTER_TYPE(ultrasound_x_pos, std::reference_wrapper<const std::vector<float>>, false);
REGISTER_TYPE(ultrasound_y_pos, std::reference_wrapper<const std::vector<float>>, false);

/*
 * Room
 * */
REGISTER_TYPE(delimiting_polygon_x, std::reference_wrapper<const std::vector<float>>, false);
REGISTER_TYPE(delimiting_polygon_y, std::reference_wrapper<const std::vector<float>>, false);
REGISTER_TYPE(room_height, float, false);
REGISTER_TYPE(room_is_oriented, bool, false);
REGISTER_TYPE(center_x, float, false);
REGISTER_TYPE(center_y, float, false);
REGISTER_TYPE(corner1, std::reference_wrapper<const std::vector<float>>, false);
REGISTER_TYPE(corner2, std::reference_wrapper<const std::vector<float>>, false);
REGISTER_TYPE(corner3, std::reference_wrapper<const std::vector<float>>, false);
REGISTER_TYPE(corner4, std::reference_wrapper<const std::vector<float>>, false);

/*
*meshes
*/
REGISTER_TYPE(mesh_vertices, std::reference_wrapper<const std::vector<float>>, false)


/* * * * * * * * * * *
 * MELEX-RODAO ATTR  *
 * * * * * * * * * * */


/*
 * Road
 * */

REGISTER_TYPE(road_name, std::reference_wrapper<const std::string>, false)

/*
 * Building
 * */

REGISTER_TYPE(building_name, std::reference_wrapper<const std::string>, false)

/*
 * Vehicle
 * */

REGISTER_TYPE(vehicle_id, int, false)
REGISTER_TYPE(vehicle_occupancy, bool, false)
REGISTER_TYPE(vehicle_throttle, float, false)
REGISTER_TYPE(vehicle_steer, float, false)
REGISTER_TYPE(vehicle_brake, float, false)
REGISTER_TYPE(vehicle_gear, int, false)
REGISTER_TYPE(vehicle_manual_gear, bool, false)
REGISTER_TYPE(vehicle_handbrake, bool, false)
REGISTER_TYPE(vehicle_reverse, bool, false)

/*
 * Camera
 * */
REGISTER_TYPE(cam_id, int, false)
REGISTER_TYPE(cam_name, std::reference_wrapper<const std::string>, false)
REGISTER_TYPE(cam_image, std::reference_wrapper<const std::vector<uint8_t>>, true)
REGISTER_TYPE(cam_width, int, false)
REGISTER_TYPE(cam_height, int, false)
REGISTER_TYPE(cam_is_on, bool, false)
REGISTER_TYPE(cam_time_stamp, float, false)
REGISTER_TYPE(cam_sensor_tick, float, false)
REGISTER_TYPE(cam_fov, float, false)
// Equirectangular (360) intrinsics: panorama column convention (mirror sign ±1, seam zero offset).
REGISTER_TYPE(cam_equirect_azimuth_sign, float, false)
REGISTER_TYPE(cam_equirect_azimuth_offset, float, false)
// 360-panorama projection model: "equirectangular" (spherical, elevation∝angle) or "cylindrical" (webots
// camera: azimuth linear, ELEVATION planar ∝ tan). Selects CameraAPI's vertical map; azimuth is common.
REGISTER_TYPE(cam_projection, std::reference_wrapper<const std::string>, false)

/*
 * GPS
 * */
REGISTER_TYPE(gps_id, int, false)
REGISTER_TYPE(gps_latitude, float, false)
REGISTER_TYPE(gps_longitude, float, false)
REGISTER_TYPE(gps_altitude, float, false)
REGISTER_TYPE(gps_time_stamp, float, false)
REGISTER_TYPE(gps_sensor_tick, float, false)
REGISTER_TYPE(gps_map_x, float, false)
REGISTER_TYPE(gps_map_y, float, false)
REGISTER_TYPE(gps_azimut, float, false)
REGISTER_TYPE(gps_rot, float, false)
REGISTER_TYPE(gps_UTMx, float, false)
REGISTER_TYPE(gps_UTMy, float, false)

/*
 * IMU
 * */

REGISTER_TYPE(imu_id, int, false)
REGISTER_TYPE(imu_accelerometer, std::reference_wrapper<const std::vector<float>>, false)
REGISTER_TYPE(imu_gyroscope, std::reference_wrapper<const std::vector<float>>, false)
REGISTER_TYPE(imu_compass, float, false)
REGISTER_TYPE(imu_time_stamp, uint64_t, false)
REGISTER_TYPE(imu_sensor_tick, uint64_t, false)

REGISTER_TYPE(imu_linear_pose, std::reference_wrapper<const std::vector<float>>, false)
REGISTER_TYPE(imu_angular_euler_xyz_pose, std::reference_wrapper<const std::vector<float>>, false)
REGISTER_TYPE(imu_angular_quaternion_pose, std::reference_wrapper<const std::vector<float>>, false)
REGISTER_TYPE(imu_linear_velocity, std::reference_wrapper<const std::vector<float>>, false)
REGISTER_TYPE(imu_angular_velocity, std::reference_wrapper<const std::vector<float>>, false)
REGISTER_TYPE(imu_linear_acceleration, std::reference_wrapper<const std::vector<float>>, false)
REGISTER_TYPE(imu_angular_acceleration, std::reference_wrapper<const std::vector<float>>, false)



/*
 * SERVO
 * */

REGISTER_TYPE(servo_ref_pos, float, false)
REGISTER_TYPE(servo_ref_speed, float, false)
REGISTER_TYPE(servo_pos, float, false)
REGISTER_TYPE(servo_speed, float, false)
REGISTER_TYPE(servo_moving, bool, false)
	
/*
 * Agents
 * */
REGISTER_TYPE(timestamp_agent, uint64_t, false)
REGISTER_TYPE(timestamp_creation, uint64_t, false)
REGISTER_TYPE(timestamp_alivetime, uint64_t, false)
REGISTER_TYPE(agent_id, uint32_t , false)
REGISTER_TYPE(agent_name, std::reference_wrapper<const std::string>, false)
REGISTER_TYPE(active_agent, bool, false)
REGISTER_TYPE(cpu_usage, float, false)
REGISTER_TYPE(memory_usage, uint32_t , false)
REGISTER_TYPE(num_procs, uint32_t, false)
REGISTER_TYPE(agent_description, std::reference_wrapper<const std::string>, false)


/*
* WAYP
* */
REGISTER_TYPE(wayp_id, int, false)
REGISTER_TYPE(wayp_x, int, false)
REGISTER_TYPE(wayp_y, int, false)
REGISTER_TYPE(wayp_time_stamp, float, false)
REGISTER_TYPE(wayp_sensor_tick, float, false)

/*
* TASK
* */
REGISTER_TYPE(task_id, int, false)
REGISTER_TYPE(task_assigned, bool, false)
REGISTER_TYPE(task_car, int, false)
REGISTER_TYPE(task_completed, bool, true)
REGISTER_TYPE(task_creation, uint64_t, false)
REGISTER_TYPE(task_movement, bool, false)
REGISTER_TYPE(task_pickup_values, std::reference_wrapper<const std::vector<float>>, false)
REGISTER_TYPE(task_destination_values, std::reference_wrapper<const std::vector<float>>, false)
REGISTER_TYPE(task_time_stamp, float, false)
REGISTER_TYPE(task_sensor_tick, float, false)

/*
* ROOM
* */
REGISTER_TYPE(corner_id, int, false)
REGISTER_TYPE(valid, bool, false)

/*
* DOOR
* */
REGISTER_TYPE(other_side_door_name, std::reference_wrapper<const std::string>, false)
REGISTER_TYPE(connected_room_name, std::reference_wrapper<const std::string>, false)

/*
* INTENTION EDGE
* */
/* agent_id and valid are already defined */
REGISTER_TYPE(state, std::string, false) /* State of the intention edge { waiting, in_progress, aborted, failed, completed} */
REGISTER_TYPE(offset_xyz, std::reference_wrapper<const std::vector<float>>, false)  /* 3-vector Offset for the edge */
REGISTER_TYPE(orientation, std::reference_wrapper<const std::vector<float>>, false) /* 3-vector Orientation for the offset */
REGISTER_TYPE(tolerance, std::reference_wrapper<const std::vector<float>>, false)  /* 6-vector Tolerance for the offset and orientation*/
REGISTER_TYPE(subsystem, std::string, false) /* Subsystem that will execute the intention {base, arm_0, head}*/
REGISTER_TYPE(bt_state, std::string, false) /* Subsystem that will execute the intention {base, arm_0, head}*/


/* TESTS
 * */

typedef std::array<float, 2> vec2;
typedef std::array<float, 3> vec3;
typedef std::array<float, 4> vec4;
typedef std::array<float, 6> vec6;


REGISTER_TYPE(test_string_type, std::string, false)
REGISTER_TYPE(test_double_type, double, false)
REGISTER_TYPE(test_uint32_type, uint32_t , false)
REGISTER_TYPE(test_uint64_vec_type, std::reference_wrapper<const std::vector<uint64_t>> , false)
REGISTER_TYPE(test_vec2_type, std::reference_wrapper<const vec2> , false)
REGISTER_TYPE(test_vec3_type, std::reference_wrapper<const vec3> , false)
REGISTER_TYPE(test_vec4_type, std::reference_wrapper<const vec4> , false)
REGISTER_TYPE(test_vec6_type, std::reference_wrapper<const vec6> , false)

/*
*COMPRESSED
*/
REGISTER_TYPE(compresssed_id, int, false)
REGISTER_TYPE(compressed_data, std::reference_wrapper<const std::vector<uint8_t>>, false)

// NEW ATTRIBUTES FOR ACTIVE INFERENCE AGENTS
// ── table-concept sensing interface (written by robot_concept, read by table-concept) ───────────
REGISTER_TYPE(candidate_pts,       std::reference_wrapper<const std::vector<float>>, false)
REGISTER_TYPE(residual_pts,        std::reference_wrapper<const std::vector<float>>, false)
REGISTER_TYPE(residual_mass,       int,                                               false)
REGISTER_TYPE(explanation_ratio,   float,                                             false)
REGISTER_TYPE(last_sensing_frame,  int,                                               false)
REGISTER_TYPE(rfe_pts,             std::reference_wrapper<const std::vector<float>>, false)

// ── residual_concept occupancy-GRID costmap (written by residual_concept on the `grid` node; occupied +
//    inflated-border cell centres for display; encoded obstacle hulls for the controller's planner) ──────
REGISTER_TYPE(grid_occupied_cells, std::reference_wrapper<const std::vector<float>>, false)  // [x,y,z]×N occupied
REGISTER_TYPE(grid_border_cells,   std::reference_wrapper<const std::vector<float>>, false)  // [x,y,z]×N inflated ring
REGISTER_TYPE(grid_obstacle_hulls, std::reference_wrapper<const std::vector<float>>, false)  // [P,(V,x,y…)×P] footprints
REGISTER_TYPE(grid_cell_size,      float,                                            false)  // cell edge (m)
// ── Beta–Bernoulli BELIEF FIELD (dense, row-major over the grid extent) — the planner plans over belief:
//    grid_occupancy_prob = mean occupancy P (collision RISK); grid_occupancy_var = Var[P] (EPISTEMIC term).
//    grid_field_meta = [xmin, ymin, cell_size, width, height] to interpret the row-major arrays. Cells EXPLAINED
//    by a modelled object are collapsed to (P=0, Var=0) at publish (the object agent owns that region). ──
REGISTER_TYPE(grid_occupancy_prob, std::reference_wrapper<const std::vector<float>>, false)  // dense P, row-major
REGISTER_TYPE(grid_occupancy_var,  std::reference_wrapper<const std::vector<float>>, false)  // dense Var[P], row-major
REGISTER_TYPE(grid_field_meta,     std::reference_wrapper<const std::vector<float>>, false)  // [xmin,ymin,cell,w,h]


// ── table-concept inference outputs (written by table-concept) ────────────────────────────────
REGISTER_TYPE(free_energy,         float,                                             false)
REGISTER_TYPE(model_stable,        bool,                                              false)
REGISTER_TYPE(model_generation,    int,                                               false)
REGISTER_TYPE(model_uncertainty,   float,                                             false)
REGISTER_TYPE(request_full_sample, bool,                                              false)

// ── epistemic action proposal (written by table-concept, read by mission-controller) ────────
REGISTER_TYPE(epistemic_target_x_m,   float,                                             false)
REGISTER_TYPE(epistemic_target_y_m,   float,                                             false)
REGISTER_TYPE(epistemic_target_yaw_rad,float,                                            false)
REGISTER_TYPE(epistemic_gain,          float,                                             false)
REGISTER_TYPE(epistemic_pending,       bool,                                              false)

// ── semantic labeling attributes (written by voxelizer, read by table-concept) ───────────────
REGISTER_TYPE(mask_frame_id,          int,                                               false)
REGISTER_TYPE(mask_count,             int,                                               false)
REGISTER_TYPE(mask_labels,            std::reference_wrapper<const std::string>,         false)
REGISTER_TYPE(mask_label_ids,         std::reference_wrapper<const std::vector<float>>,  false)
REGISTER_TYPE(mask_confidences,       std::reference_wrapper<const std::vector<float>>,  false)
REGISTER_TYPE(mask_support_offsets,   std::reference_wrapper<const std::vector<float>>,  false)
REGISTER_TYPE(mask_support_points,    std::reference_wrapper<const std::vector<float>>,  false)
REGISTER_TYPE(mask_centroids_xyz,     std::reference_wrapper<const std::vector<float>>,  false)
REGISTER_TYPE(mask_bbox_min_xyz,      std::reference_wrapper<const std::vector<float>>,  false)
REGISTER_TYPE(mask_bbox_max_xyz,      std::reference_wrapper<const std::vector<float>>,  false)

#endif //DSR_ATTR_NAME_H
