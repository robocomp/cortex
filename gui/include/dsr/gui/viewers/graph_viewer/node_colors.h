//
// Created by robolab on 19/5/21.
//

#ifndef DSR_NODE_COLORS_H
#define DSR_NODE_COLORS_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

/*
 * Node colours, keyed by node type.
 *
 * Grouped into semantic FAMILIES so a graph reads at a glance: blues are the robot platform,
 * cyans/teals are sensors, greens are cognition/navigation, browns are structure and furniture,
 * pinks/magentas are a detected person. You should be able to spot "that is a sensor" without
 * reading the label.
 *
 * Two rules to preserve when editing:
 *
 *  1. `agent` MUST stay a neutral grey. It is the "this agent has not reported its health yet"
 *     state; rc::AgentStatePublisher overwrites it live with green/orange/red (see
 *     common/agent_state_publisher/agent_status.h in active_inference). Giving `agent` a real
 *     colour here would make a silent or dead agent look meaningful.
 *  2. Avoid plain "green" / "orange" / "red" for ordinary node types, so those three keep reading
 *     as agent health rather than as a node category.
 *
 * Every value must be a CSS3 colour name (list at the bottom of this file). QColor yields BLACK for
 * anything it cannot parse; GraphNode::set_color now rejects and logs such values rather than
 * painting the node black, but keep them valid anyway.
 *
 * A type NOT listed here no longer collapses onto one flat default — see stable_fallback_color().
 */
static const std::map<std::string, std::string> node_colors = {
// -- Structure / world ------------------------------------------------------------------------
        { "root", "red"},                   // the single graph root; long-standing, kept
        { "transform", "SteelBlue"},
        { "mind", "MediumPurple"},          // parent of the agent nodes
        { "room", "Tan"},
        { "wall", "CadetBlue"},
        { "floor", "BurlyWood"},
        { "plane", "Khaki"},
        { "grid", "Wheat"},
        { "mesh", "LightBlue"},

// -- Robot platform ---------------------------------------------------------------------------
        { "robot", "RoyalBlue"},
        { "omnirobot", "CornflowerBlue"},
        { "differentialrobot", "GoldenRod"},
        { "body", "LightSteelBlue"},
        { "pan_tilt", "PowderBlue"},
        { "battery", "YellowGreen"},

// -- Sensors ----------------------------------------------------------------------------------
        { "rgbd", "MediumTurquoise"},
        { "camera", "Turquoise"},
        { "laser", "GreenYellow"},
        { "imu", "LightSalmon"},
        { "slam_device", "DarkCyan"},
        { "gps", "DeepSkyBlue"},

// -- Cognition / navigation -------------------------------------------------------------------
        { "path_to_target", "SpringGreen"},
        { "intention", "MediumSpringGreen"},
        { "pose", "Aquamarine"},
        { "affordance", "DarkOrange"},
        { "affordance_space", "Coral"},
        { "object", "DarkSeaGreen"},

// -- Geometric primitives ---------------------------------------------------------------------
        { "box", "SlateBlue"},
        { "cylinder", "MediumSlateBlue"},
        { "ball", "DarkSlateBlue"},

// -- Furniture / household objects --------------------------------------------------------------
        { "table", "Peru"},
        { "chair", "Sienna"},
        { "shelve", "Chocolate"},
        { "microwave", "DarkOliveGreen"},
        { "oven", "IndianRed"},
        { "refrigerator", "PaleTurquoise"},
        { "vase", "MediumSeaGreen"},
        { "plant", "ForestGreen"},
        { "mug", "Plum"},
        { "cup", "Violet"},
        { "glass", "PaleGreen"},
        { "dish", "Moccasin"},
        { "spoon", "PaleGoldenRod"},
        { "noodles", "NavajoWhite"},
        { "testtype", "Gold"},

// -- Person & skeleton --------------------------------------------------------------------------
// One pink/magenta family so a detected person reads as a single cluster. Left/right pairs share a
// colour ON PURPOSE (the symmetry is the point), and joints are separated from limbs: 22 near
// identical pinks would carry less information than two clearly different ones.
        { "person", "HotPink"},
        { "personal_space", "Pink"},
        { "face", "LightPink"},
        { "nose", "PaleVioletRed"},
        { "left_eye", "Orchid"},
        { "right_eye", "Orchid"},
        { "left_ear", "Orchid"},
        { "right_ear", "Orchid"},
        { "chest", "DeepPink"},
        // joints
        { "left_shoulder", "MediumOrchid"},
        { "right_shoulder", "MediumOrchid"},
        { "left_elbow", "MediumOrchid"},
        { "right_elbow", "MediumOrchid"},
        { "left_wrist", "MediumOrchid"},
        { "right_wrist", "MediumOrchid"},
        { "left_hip", "MediumOrchid"},
        { "right_hip", "MediumOrchid"},
        { "left_knee", "MediumOrchid"},
        { "right_knee", "MediumOrchid"},
        // limbs / extremities
        { "left_arm", "MediumVioletRed"},
        { "right_arm", "MediumVioletRed"},
        { "left_hand", "MediumVioletRed"},
        { "right_hand", "MediumVioletRed"},
        { "left_leg", "MediumVioletRed"},
        { "right_leg", "MediumVioletRed"},

// -- melex-rodao types ---------------------------------------------------------------------------
        { "road", "DarkKhaki"},
        { "building", "SaddleBrown"},
        { "vehicle", "DarkSalmon"},

// -- Agent ---------------------------------------------------------------------------------------
// KEEP NEUTRAL GREY - see rule 1 above. This is the "health not reported yet" colour; the live
// value is written over it by rc::AgentStatePublisher.
        { "agent", "Gray"}
};

/*
 * Colour for a type with no entry above.
 *
 * Unlisted types used to collapse onto a single flat default, so a graph full of new types was a
 * wall of identical nodes carrying no information. Instead, hash the type name into a curated
 * palette: distinct, readable, never grey, never the three agent-health colours.
 *
 * The hash is FNV-1a spelled out here rather than std::hash, because the colour must be THE SAME in
 * every agent's viewer and across rebuilds - std::hash offers no such guarantee. The same type name
 * always yields the same colour, and a newly introduced type gets a distinct one for free.
 */
inline std::string stable_fallback_color(const std::string &type)
{
    static const std::vector<std::string> palette = {
        "DodgerBlue",    "MediumAquamarine", "SandyBrown",  "MediumOrchid",
        "LightSeaGreen", "Chocolate",        "CadetBlue",   "DarkKhaki",
        "PaleVioletRed", "OliveDrab",        "SlateBlue",   "MediumPurple",
        "DarkTurquoise", "IndianRed",        "Thistle",     "YellowGreen",
    };
    std::uint32_t h = 2166136261u;                 // FNV-1a offset basis
    for (const unsigned char c : type)
    {
        h ^= c;
        h *= 16777619u;                            // FNV-1a prime
    }
    return palette[h % palette.size()];
}

#endif //DSR_NODE_COLORS_H

// VALID COLOR NAMES https://www.w3.org/TR/2018/REC-css-color-3-20180619/
//aliceblue
//antiquewhite
//aqua
//aquamarine
//azure
//beige
//bisque
//Gray
//blanchedalmond
//blue
//blueviolet
//brown
//burlywood
//cadetblue
//chartreuse
//chocolate
//coral
//cornflowerblue
//cornsilk
//crimson
//cyan
//deeppink
//deepskyblue
//dimgray
//dimgrey
//dodgerblue
//firebrick
//floralwhite
//forestgreen
//fuchsia
//gainsboro
//ghostwhite
//gold
//goldenrod
//gray
//green
//greenyellow
//grey
//honeydew
//hotpink
//indianred
//indigo
//ivory
//khaki
//lavender
//lavenderblush
//lawngreen
//lemonchiffon
//lightblue
//lightcoral
//lightcyan
//lightgoldenrodyellow
//lightgray
//lightgreen
//lightgrey
//lightpink
//lightsalmon
//lightseagreen
//lightskyblue
//lightslategray
//lightslategrey
//lightsteelblue
//lightyellow
//lime
//limegreen
//linen
//magenta
//maroon
//mediumaquamarine
//mediumblue
//mediumorchid
//mediumpurple
//mediumseagreen
//mediumslateblue
//mediumspringgreen
//mediumturquoise
//mediumvioletred
//midnightblue
//mintcream
//mistyrose
//moccasin
//navajowhite
//navy
//oldlace
//olive
//olivedrab
//orange
//orangered
//orchid
//palegoldenrod
//palegreen
//paleturquoise
//palevioletred
//papayawhip
//peachpuff
//peru
//pink
//plum
//powderblue
//purple
//red
//rosybrown
//royalblue
//saddlebrown
//salmon
//sandybrown
//seagreen
//seashell
//sienna
//silver
//skyblue
//slateblue
//slategray
//slategrey
//snow
//springgreen
//steelblue
//tan
//teal
//thistle
//tomato
//turquoise
//violet
//wheat
//white
//whitesmoke
//yellow
//yellowgreen