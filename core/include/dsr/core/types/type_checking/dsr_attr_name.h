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
// Per-frame ROBOT-frame observation of a modelled object (object-anchor z_o): [x,y] or [x,y,yaw],
// plus optional diagonal measurement covariance. Written by concept agents (table/chair/…), read by
// the room localizer to use the object as an SE(2) pose landmark. See common/object_anchor.
REGISTER_TYPE(obj_obs_robot,     std::reference_wrapper<const std::vector<float>>, true)
REGISTER_TYPE(obj_obs_robot_cov, std::reference_wrapper<const std::vector<float>>, true)
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
REGISTER_TYPE(robot_current_speed_timestamp, uint64_t, true)  // wall clock, epoch ms
// Simulation clock, ms since world load, for the sample robot_current_*_speed came from; 0 when
// robot_current_speed_simulated is false. A simulator's velocities are per SIMULATION second, so
// anything integrating them (a high-rate propagation between optimized poses, say) must integrate
// over THIS clock or it over-counts by the sim/wall ratio. The wall stamp above stays authoritative
// for latency and staleness, in simulation too.
REGISTER_TYPE(robot_current_speed_sim_timestamp, uint64_t, true)
REGISTER_TYPE(robot_current_speed_simulated, bool, true)
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
REGISTER_TYPE(imu_time_stamp, uint64_t, false)                // wall clock, epoch ms
// Simulation clock, ms since world load, for the sample the imu_* attributes above came from; 0 when
// imu_simulated is false. A simulated gyro reports rad per SIMULATION second, so a consumer
// integrating it must integrate over THIS clock or it over-counts by however far the sim is running
// behind real time. imu_time_stamp stays authoritative for latency and staleness.
REGISTER_TYPE(imu_sim_time_stamp, uint64_t, false)
REGISTER_TYPE(imu_simulated, bool, false)
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
// Deployment metadata, self-reported once at agent-node creation (see AgentInfoAPI). Feeds the
// "mind" node network view: agent_cmd/agent_cwd are the launch command + working dir read from
// /proc/self, agent_config is the raw contents of the etc/config passed on the command line.
REGISTER_TYPE(agent_cmd, std::reference_wrapper<const std::string>, false)
REGISTER_TYPE(agent_cwd, std::reference_wrapper<const std::string>, false)
REGISTER_TYPE(agent_config, std::reference_wrapper<const std::string>, false)
// Process id of the agent (getpid). Lets the mind network view attribute each loopback TCP
// connection to the exact client agent (inode→pid→agent), i.e. per-connection bandwidth.
REGISTER_TYPE(agent_pid, std::uint32_t, false)
// Live health of the agent, self-reported on every state transition (see rc::AgentStatePublisher).
// Two independent axes: the generated GRAFCET/QStateMachine step the agent is executing, and the
// agent-presence lifecycle (are its required peers there?). They are NOT redundant: an agent parked
// in on_waiting_loop waiting for peers still reports FSM "Compute". The publisher also derives a
// worst-wins `color` from the pair so any graph viewer paints the agent node without duplicating
// the mapping.
REGISTER_TYPE(agent_fsm_state, std::string, false)      /* Initialize | Compute | Emergency | Restore */
REGISTER_TYPE(agent_presence_state, std::string, false) /* Waiting | Operating | Degraded */
// Live media-plane throughput (bytes/s) the producer (SensorMediaPublisher) writes onto a sensor's
// descriptor node. The heavy frames travel over zero-copy DDS shared memory, invisible to packet
// capture, so this self-reported figure is the only way the mind view can show real media bandwidth.
REGISTER_TYPE(media_bps, float, false)


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
// REMOVED 2026-08-14: rfe_pts. bottle_concept's copy of the *_voxel_bank_pts dead write — the accumulated
//   mask-support bank, up to 4000 points x 3 floats per publish, with no reader anywhere in the tree and
//   (unlike its five siblings) no gate either. Writer removed in active_inference.

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


// ── REMOVED 2026-08-14: the four *_voxel_bank_pts registrations (table_/chair_/door_/cabinet_).
//    They were never voxels — the points are the 3-D support of a segmentation mask, split by the
//    owning model's own SDF. Six agents wrote up to 4000 points x 3 floats per object per publish into
//    the CRDT graph and NOTHING in the whole components tree read them (audited 2026-08-14). The
//    accumulated bank stays, in-process, where the round-vs-square shape decision reads it.

// ── table-concept active-perception ROI channel (written by table_concept) ─────────────────────
//    Registered 2026-07-21 so table_scene_graph.cpp can use the TYPE-ATTRIBUTED setters instead of the
//    runtime_checked_* string form (see CLAUDE.md). table_roi_offset is an [ox,oy]-packed float
//    array; the ROI + detection scalars drive the controller's lock-on search
//    via common/affordance_protocol (its attr_scalar() coerces int/float/bool alike).
//    WIRE-TYPE WARNING for whoever edits these next: an attribute's registered type is part of its CONTRACT
//    with every consumer. table_detection_alive was int 0/1 until 2026-07-21; flipping it to bool without
//    migrating the readers made room_concept throw "INT is not selected, selected is BOOL" at runtime,
//    because it read the raw attrs map with Attribute::dec(). Both readers now use the TYPE-ATTRIBUTED
//    getter (get_attrib_by_name<table_detection_alive_att>), so a future type change is a COMPILE error
//    there instead of a runtime throw. Never change a registered type without migrating every reader.
REGISTER_TYPE(table_roi_offset,             std::reference_wrapper<const std::vector<float>>, false)  // [ox,oy] ∈[-1,1]
REGISTER_TYPE(table_roi_fill,               float,                                            false)  // projected extent frac
REGISTER_TYPE(table_roi_valid,              bool,                                             false)  // projects in front of cam
REGISTER_TYPE(table_detection_alive,        bool,                                             false)  // YOLO firing here
REGISTER_TYPE(table_detection_confidence,   float,                                            false)  // last mask confidence
REGISTER_TYPE(table_frames_since_detection, int,                                              false)  // cycles since fresh mask

// ── Object SUBTYPE (shape model-selection) — generic across concept agents (registered 2026-07-24) ────
//    A free-form string signalling the fitted object's inferred sub-shape, chosen by free-energy / model
//    evidence (e.g. table = "round" | "square"). Free-form so finer sub-subtypes are just longer strings
//    ("round_pedestal", …). Written by the concept agent on its object node; read by the voxelizer to
//    render the matching mesh (e.g. a disc vs a box) and by any consumer that cares about shape.
REGISTER_TYPE(object_subtype,               std::string,                                      false)

// ── Display-mesh handoff: a concept agent owns its own appearance and publishes the relative asset path(s)
//    on its node; the voxelizer's 3D viewer reads them, loads the mesh (cached by path), and renders it
//    scaled to the node's fitted box — no per-type knowledge in the viewer. Contract for the referenced OBJ:
//    room-frame Z-up, footprint centred at origin (x,y ∈ [-0.5,0.5]), height z ∈ [0,1], front baked to the
//    node's yaw-zero. Paths are RELATIVE (resolved by each consumer against its own assets root, e.g. the
//    voxelizer's meshes/). mesh_texture_path is the optional base-colour image; empty ⇒ flat class colour.
REGISTER_TYPE(mesh_path,                     std::string,                                      false)
REGISTER_TYPE(mesh_texture_path,             std::string,                                      false)
// mesh_color_rgb: [r,g,b] CHROMATICITY (each in [0,1], summing to ~1), the MAP of the concept agent's
//    per-instance appearance belief (common/appearance_belief). DISPLAY ONLY — it never enters any
//    geometric fit. Chromaticity, not raw RGB, because the viewer applies its own ambient+diffuse
//    shading; handing it observed RGB would bake the room's lighting in and then shade it again.
//    The viewer applies it as a NORMALISED MULTIPLICATIVE tint against the asset's own mean
//    chromaticity, so authored inter-material contrast survives and an absent/unconfident belief is
//    the identity. Absent ⇒ render the asset's authored .mtl colours unchanged. Only 3 floats, so a
//    plain vector (no reference_wrapper) — the zero-copy form is for the big per-slice blobs.
REGISTER_TYPE(mesh_color_rgb,                std::vector<float>,                               false)

// ── chair-concept / cabinet-concept: same voxel-memory + ROI channel as table-concept above ────
//    These agents are copies of the table_concept pattern and carry the identical interface, so they get
//    the identical types. Readers are common/affordance_protocol (attr_scalar coerces int/float/bool), and
//    no consumer reads them with a raw Attribute accessor — verified 2026-07-21 before choosing bool.
REGISTER_TYPE(chair_roi_offset,               std::reference_wrapper<const std::vector<float>>, false)  // [ox,oy]
REGISTER_TYPE(chair_roi_fill,                 float,                                            false)
REGISTER_TYPE(chair_roi_valid,                bool,                                             false)
REGISTER_TYPE(chair_detection_alive,          bool,                                             false)
REGISTER_TYPE(chair_detection_confidence,     float,                                            false)
REGISTER_TYPE(chair_frames_since_detection,   int,                                              false)
REGISTER_TYPE(door_roi_offset,                std::reference_wrapper<const std::vector<float>>, false)  // [ox,oy]
REGISTER_TYPE(door_roi_fill,                  float,                                            false)
REGISTER_TYPE(door_roi_valid,                 bool,                                             false)
REGISTER_TYPE(door_detection_alive,           bool,                                             false)
REGISTER_TYPE(door_detection_confidence,      float,                                            false)
REGISTER_TYPE(door_frames_since_detection,    int,                                              false)
REGISTER_TYPE(cabinet_roi_offset,             std::reference_wrapper<const std::vector<float>>, false)  // [ox,oy]
REGISTER_TYPE(cabinet_roi_fill,               float,                                            false)
REGISTER_TYPE(cabinet_roi_valid,              bool,                                             false)
REGISTER_TYPE(cabinet_detection_alive,        bool,                                             false)
REGISTER_TYPE(cabinet_detection_confidence,   float,                                            false)
REGISTER_TYPE(cabinet_frames_since_detection, int,                                              false)

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

// ── affordance REFUSAL (written by the consumer, read by the producer) ───────────────────────
// "I could not get there." Distinct from a completion in the one way that matters: Completed means
// OBSERVED — update the belief, reset the neglect clock; Refused means NOT ATTEMPTED — change nothing
// that is believed, only where to stand. Conflating them makes an agent confident about something it
// never saw. The refusal retires the current offer; the producer publishes a different standpoint when
// it has one, and publish_target already declines to re-arm an unchanged target, so an unchanged
// proposal is simply not news and the exchange terminates without a handshake.
// The POSE is carried so the producer can exclude that viewpoint from its next ranking instead of
// re-deriving the same best answer and being refused again. It is the pose AS PUBLISHED, before any
// consumer-side repair moved it.
REGISTER_TYPE(epistemic_refused,       bool,                                              false)
REGISTER_TYPE(epistemic_refused_x_m,   float,                                             false)
REGISTER_TYPE(epistemic_refused_y_m,   float,                                             false)

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
// ── mask channel, continued (registered 2026-07-22 so voxelizer/graph_publisher.cpp and common/mask_ingestor
//    can use the TYPE-ATTRIBUTED API instead of runtime_checked_* string writes / attrs.find()+dec() reads.
//    ⚠ mask_timestamp_ms is uint64_t — the ingestor reads it with ->uint64(); do NOT copy the mask_frame_id
//    int pattern for it. The vector<float> ones use reference_wrapper for zero-copy reads, matching the block
//    above; they are still SET by passing a plain std::vector<float> by value. ──
REGISTER_TYPE(mask_timestamp_ms,      std::uint64_t,                                     false)
REGISTER_TYPE(mask_support_points_cam,std::reference_wrapper<const std::vector<float>>,  false)
REGISTER_TYPE(mask_pixels_xy,         std::reference_wrapper<const std::vector<float>>,  false)
REGISTER_TYPE(mask_pixel_offsets,     std::reference_wrapper<const std::vector<float>>,  false)
REGISTER_TYPE(mask_has_depth,         std::reference_wrapper<const std::vector<float>>,  false)
REGISTER_TYPE(mask_source,            std::reference_wrapper<const std::vector<float>>,  false)
REGISTER_TYPE(mask_azimuth,           std::reference_wrapper<const std::vector<float>>,  false)
REGISTER_TYPE(mask_depth_var,         std::reference_wrapper<const std::vector<float>>,  false)
REGISTER_TYPE(mask_motion_dotd,       std::reference_wrapper<const std::vector<float>>,  false)
REGISTER_TYPE(mask_motion_bias,       std::reference_wrapper<const std::vector<float>>,  false)
REGISTER_TYPE(mask_motion_var,        std::reference_wrapper<const std::vector<float>>,  false)
REGISTER_TYPE(mask_trunc_frac,        std::reference_wrapper<const std::vector<float>>,  false)
REGISTER_TYPE(mask_centroid_radius,   std::reference_wrapper<const std::vector<float>>,  false)
REGISTER_TYPE(mask_range,             std::reference_wrapper<const std::vector<float>>,  false)
REGISTER_TYPE(mask_cam_twist,         std::reference_wrapper<const std::vector<float>>,  false)
// ── per-mask APPEARANCE channel (registered 2026-07-27; written by voxelizer/graph_publisher.cpp, read by
//    common/mask_ingestor → the concept agents' appearance belief). One summary per slice, NOT per pixel:
//    mask_color_rgb  — 3 per slice: median CHROMATICITY (R,G,B)/(R+G+B) over the slice's interior grid cells.
//                      Chromaticity because it is invariant to the per-frame illumination gain by
//                      construction, which is the dominant nuisance in observed pixel colour.
//    mask_color_var  — 3 per slice: BETWEEN-CELL variance of that chromaticity. This is the honest
//                      uncertainty: neighbouring pixels on one surface are massively correlated, so a
//                      per-pixel variance would collapse σ like 1/√N and make the channel absurdly
//                      overconfident. Aggregating per grid cell handles the correlation by construction.
//    mask_color_neff — 1 per slice: count of contributing INTERIOR cells (a cell counts only if its
//                      4-neighbours are also occupied, which erodes the silhouette boundary for free and
//                      so keeps background bleed out). 0 ⇒ no colour information this frame; consumers
//                      simply gain nothing, no branch needed. A far/small object yields few cells and
//                      therefore little information — a range gate falls out of the model, not an if.
REGISTER_TYPE(mask_color_rgb,         std::reference_wrapper<const std::vector<float>>,  false)
REGISTER_TYPE(mask_color_var,         std::reference_wrapper<const std::vector<float>>,  false)
REGISTER_TYPE(mask_color_neff,        std::reference_wrapper<const std::vector<float>>,  false)
REGISTER_TYPE(mask_frame_dt_s,        float,                                             false)
REGISTER_TYPE(mask_rt_lag_s,          float,                                             false)
REGISTER_TYPE(mask_rt_gap_s,          float,                                             false)
// ── human skeleton channel (voxelizer → human_concept / robot_concept viewer). skeleton_timestamp_ms is
//    uint64_t (read via ->uint64()), same trap as mask_timestamp_ms. ──
REGISTER_TYPE(skeleton_frame_id,      int,                                               false)
REGISTER_TYPE(skeleton_timestamp_ms,  std::uint64_t,                                     false)
REGISTER_TYPE(skeleton_count,         int,                                               false)
REGISTER_TYPE(skeleton_ids,           std::reference_wrapper<const std::vector<float>>,  false)
REGISTER_TYPE(skeleton_kp_xyz,        std::reference_wrapper<const std::vector<float>>,  false)
REGISTER_TYPE(skeleton_kp_conf,       std::reference_wrapper<const std::vector<float>>,  false)

// ── dense semantic segmentation label map (ADE20K-150). Written LOW-FREQUENCY by voxelizer on a
//    'semantic' node under 'zed'. semantic_labels is CV_8UC1 class ids, row-major, at the ZED IMAGE
//    resolution (already unletterboxed) → a consumer reads label = semantic_labels[v*width + u] directly.
REGISTER_TYPE(semantic_labels,        std::reference_wrapper<const std::vector<uint8_t>>, false)
REGISTER_TYPE(semantic_width,         int,                                                false)
REGISTER_TYPE(semantic_height,        int,                                                false)
REGISTER_TYPE(semantic_timestamp_ms,  uint64_t,                                           false)
REGISTER_TYPE(semantic_frame_id,      int,                                                false)

// ── affordance / viewpoint CONTRACT (common/affordance_protocol.h, written by EVERY concept agent) ──
//    Registered 2026-07-24 so write_contract()/write_viewpoint() use the TYPE-ATTRIBUTED setters instead
//    of runtime_checked_* string writes (see CLAUDE.md). The read side (read_contract/attr_scalar/
//    attr_string) does defensive attrs.find() lookups with defaults and coerces int/float/bool alike, so
//    it is type-agnostic and unaffected by this. Scalars=float, counts=int, names/enum-strings=std::string,
//    packed lists='|'-joined std::string or std::vector<float>.
REGISTER_TYPE(aff_policy,               std::string,                                      false)
REGISTER_TYPE(aff_err_vec_attr,         std::string,                                      false)
REGISTER_TYPE(aff_scalar_attr,          std::string,                                      false)
REGISTER_TYPE(aff_scalar_target,        float,                                            false)
REGISTER_TYPE(aff_valid_attr,           std::string,                                      false)
REGISTER_TYPE(aff_goal_attrs,           std::string,                                      false)  // '|'-joined names
REGISTER_TYPE(aff_goal_ops,             std::string,                                      false)  // '|'-joined ops
REGISTER_TYPE(aff_goal_values,          std::vector<float>,                               false)
REGISTER_TYPE(aff_goal_stable_n,        int,                                              false)
REGISTER_TYPE(aff_timeout_ms,           float,                                            false)
REGISTER_TYPE(aff_on_fail,              std::string,                                      false)
REGISTER_TYPE(aff_max_vel,              float,                                            false)  // m/s
REGISTER_TYPE(aff_max_omega,            float,                                            false)  // rad/s
REGISTER_TYPE(aff_view_object_relative, int,                                              false)  // 0/1 flag
REGISTER_TYPE(aff_view_faces,           std::string,                                      false)  // '|'-joined faces
REGISTER_TYPE(aff_view_face_gains,      std::vector<float>,                               false)
REGISTER_TYPE(aff_view_standoff_min,    float,                                            false)
REGISTER_TYPE(aff_view_standoff_max,    float,                                            false)
REGISTER_TYPE(aff_view_framing_fill,    float,                                            false)
REGISTER_TYPE(aff_view_sigma_star,      std::vector<float>,                               false)

// WHY the affordance ended, stamped by the EXECUTOR at the terminal transition and read by the
// producer. "" / absent = not terminal yet.
//   satisfied — the completion predicate held for stable_n cycles: an OBSERVATION happened, so the
//               producer may update its belief and reset the neglect clock
//   timeout   — the predicate never held within aff_timeout_ms: nothing was observed
//   refused   — the consumer could not get there (see epistemic_refused_*): not attempted at all
//   abandoned — the operator or a higher-priority interrupt ended it
// ★ Completed is NOT NEUTRAL, and this attribute is the whole point: without it a producer cannot
// distinguish "I looked and saw" from "I gave up", so it books an observation it never got and
// retires the very affordance that would have gone back for it. Three agents currently make exactly
// that conflation because the wire carries no way to tell them apart.
REGISTER_TYPE(aff_outcome,              std::string,                                      false)

// ── human-concept + bottle-concept detection channel (mirror of table/chair/cabinet above; bool alive per
//    the WIRE-TYPE note there, read only via attr_scalar/type-attributed getters) ─ registered 2026-07-24 ─
REGISTER_TYPE(human_detection_alive,       bool,                                          false)
REGISTER_TYPE(human_detection_confidence,  float,                                         false)
REGISTER_TYPE(bottle_detection_alive,      bool,                                          false)
REGISTER_TYPE(bottle_detection_confidence, float,                                         false)

// ── residual-concept footprint hull (written by residual_scene_graph.cpp) ─ registered 2026-07-24 ──────
REGISTER_TYPE(footprint_hull,           std::reference_wrapper<const std::vector<float>>, false)  // [x,y]×V polygon

// ── kinova arm joint-buffer channel (kinova_controller) ─ registered 2026-07-24 ───────────────────────
//    ⚠ joint_buffer_base_ms is uint64_t (ring base stamp, ms) — read via ->uint64(), NOT the int pattern.
REGISTER_TYPE(joint_buffer_base_ms,      std::uint64_t,                                   false)
REGISTER_TYPE(joint_buffer_stamp_off_ms, std::vector<float>,                              false)  // per-sample ms offset
REGISTER_TYPE(joint_buffer_q,            std::vector<float>,                              false)  // [dof]×N flattened
REGISTER_TYPE(joint_buffer_dof,          int,                                             false)

// ── media-plane self-report attrs on sensor nodes (robot_concept) ─ registered 2026-07-24 ─────────────
//    media_descriptor = per-node stream descriptor JSON; media_ice_port = relayed Ice port (string). Both
//    read via ->str(). The GENERIC helpers in common/media_transport.h + sensor_media_publisher.h that take
//    a runtime attr_name PARAM stay runtime_checked (dynamic name → no compile-time alias).
REGISTER_TYPE(media_descriptor,         std::string,                                      false)
REGISTER_TYPE(media_ice_port,           std::string,                                      false)

// ── level-2 arrangement channel (ring_metaconcept) ─ registered 2026-07-26 ───────────────────────
// The TOP-DOWN message from a meta-concept to its constituent objects: the arrangement's empirical
// prior on each member's pose. Written ONLY by the rig agent, onto the non-RT `group_member` edge
// rig→member; the member agents READ it and fuse it precision-weighted (they never write it).
//
// ★It rides an EDGE, not the member node, and that is a correctness requirement rather than taste.
// CRDTSyncEngine::update_node_raw resets every attribute present in the local registry but ABSENT
// from the submitted node — so a member agent doing get_node → modify → update_node with a copy
// fetched before the rig's delta arrived would silently DELETE the prior from its own node. The rig
// is the sole writer of the edge, so on an edge the message cannot be clobbered.
REGISTER_TYPE(rig_id,                   std::uint64_t,                                    false)  // authoring rig node (staleness / multi-rig)
REGISTER_TYPE(rig_stamp_ms,             std::uint64_t,                                    false)  // wall-clock ms the message was written
REGISTER_TYPE(rig_slot_index,           int,                                              false)  // which slot this member occupies
REGISTER_TYPE(rig_yaw_prior,            float,                                            false)  // cavity facing yaw, in the MEMBER's own yaw convention (rad)
REGISTER_TYPE(rig_yaw_kappa,            float,                                            false)  // its precision = p_ring / facing_var (rad⁻²); 0 ⇒ inert
REGISTER_TYPE(rig_slot_x,               float,                                            false)  // predicted slot position — radial prior (Phase 2)
REGISTER_TYPE(rig_slot_y,               float,                                            false)
REGISTER_TYPE(rig_slot_info_xx,         float,                                            false)  // its precision (Phase 2)
REGISTER_TYPE(rig_slot_info_yy,         float,                                            false)

// ── the rig node's own latent (viewers, and the table's round-vs-square prior) ────────────────────
REGISTER_TYPE(rig_schema,               std::string,                                      false)  // arrangement schema, e.g. "ring"
REGISTER_TYPE(rig_radius,               float,                                            false)  // fitted ring radius (m)
REGISTER_TYPE(rig_n_slots,              int,                                              false)  // evidence-selected slot count
REGISTER_TYPE(rig_logodds,              float,                                            false)  // ring vs independent-objects log-Bayes factor
REGISTER_TYPE(rig_shape_round_logodds,  float,                                            false)  // → table round-vs-square prior (Phase 2)

// ── ARRANGEMENT END PRIORS ─ registered 2026-08-11 ───────────────────────────────────────────────
// Where a member's two ENDS should be, so that a chain of members forms one continuous shape with no
// gap and no overlap at the joints. Room-frame target points; the member projects each onto its own
// chart, which keeps the message free of any assumption about the member's internal parameterisation.
//
// ★Why the ENDS and not the sizes. A run of kitchen carcasses presents one continuous front surface —
// where one cabinet ends and the next begins has no gap, no edge and no depth step, so the seams are
// not faint in the sensor data, they are ABSENT. That split cannot be recovered from a single member
// at any quality of sensing; only something seeing the whole arrangement can supply it. And the ends
// are exactly where a member has no opinion of its own: cabinet_concept declares t0/t1 FREE, with no
// prior at all, while depth is already over-determined by mask data (a standing depth prior was
// measured to be worth 1.3% against it). So this channel speaks where nothing else is speaking —
// and it corrects the sizes as a side effect, since moving an end hands a neighbour's points back to
// the neighbour and the fit follows.
//
// info is 1/σ² in m⁻². 0 (or absent) ⇒ inert, and the consumer ignores that end. Scaled by the
// arrangement's own existence probability, so a frame that is unsure pushes softly.
// ★The producer must DOWN-DATE its outstanding message before reading a member's reply, or the pair
// will converge on the frame's own echo (FACTORIZATION_ANCHORS.tex §5.1-5.2).
REGISTER_TYPE(rig_end_lo_x,             float,                                            false)
REGISTER_TYPE(rig_end_lo_y,             float,                                            false)
REGISTER_TYPE(rig_end_lo_info,          float,                                            false)
REGISTER_TYPE(rig_end_hi_x,             float,                                            false)
REGISTER_TYPE(rig_end_hi_y,             float,                                            false)
REGISTER_TYPE(rig_end_hi_info,          float,                                            false)

// ── OBJECT SIZE UNCERTAINTY ─ registered 2026-08-10 ──────────────────────────────────────────────
// The missing half of what a concept agent publishes about its instance. Every agent already writes
// width_m/depth_m/height_m, and an rt_covariance for the POSE — but nothing at all for the SIZE. So a
// consumer had no way to tell a 2.2 m well-observed run from a 0.4 m glimpse, and had to treat every
// producer's dimensions as equally certain.
//
// [var_width, var_depth, var_height], m² — the diagonal of the producer's own posterior over the
// three attributes of the SAME name on that node. Cross-terms are deliberately omitted: no current
// consumer uses them, and a covariance we do not populate is worse than one we do not claim.
//
// ★Why it matters concretely (kitchen_metaconcept, 2026-08-10): fitting a kitchen's shared worktop
// plane, two runs of the SAME kitchen reported tops 14 cm apart. With no per-member variance the fit
// had to weight them by RUN LENGTH as a proxy for fit quality — and length is a poor proxy, since a
// long run can still be badly fitted. The consensus plane was consequently dragged ~5 cm below the
// majority by one bad member. Publishing this fixes the weighting, lets an outlier be recognised as
// one, and supplies the own-precision term a level-2 agent needs to DOWN-DATE its own message before
// reading a member's reply (the self-confirmation guard — SCHEMA_GENERALITY_TODO.md §2.5).
//
// Producers must publish the BELIEF's own marginal variances, never a constant. Absent ⇒ the consumer
// falls back to its own (wide) assumption, so this is backward compatible.
REGISTER_TYPE(object_size_variance,     std::reference_wrapper<const std::vector<float>>,  false)

// ── node PROVENANCE: when was this node born? ─ registered 2026-08-06 ────────────────────────────
// Every node an active_inference agent inserts is stamped at creation with BOTH forms, by
// common/graph_provenance/creation_stamp.h (rc::provenance::stamp_creation), called immediately
// before insert_node:
//   timestamp_creation  (already registered above, in the Agents block) — ms since the Unix epoch.
//                       The machine-readable one: an age or a lifetime is then a subtraction.
//   creation_datetime   — the SAME instant as local civil time, ISO-8601 with UTC offset, e.g.
//                       "2026-08-06T14:32:07.512+0200". The human-readable one: this is what the
//                       DSR graph viewer's attribute table shows, where a bare epoch count tells
//                       nobody anything.
// Written ONCE, before the node exists in the graph, and never touched again. A re-acquired object
// (died, then seen again) legitimately gets a NEW stamp — it is a new node with a new id, and the
// name is the only thing that carries over. Do not refresh either on update_node.
REGISTER_TYPE(creation_datetime,        std::string,                                      false)

#endif //DSR_ATTR_NAME_H
