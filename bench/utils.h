
#include <dsr/api/dsr_api.h>
#include <fstream>
#include <span>
#include <stdlib.h>
#include <string>



template<typename T>
inline auto random_choose(const std::vector<T> &vec) -> T {
    int pos =(rand() % vec.size());
    return vec[pos];
}


inline auto random_string(size_t len=10) -> std::string {
    std::string chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    int pos;
    std::string str;
    while(str.size() != len) {
        pos = (rand() % chars.size());
        str += chars.substr(pos,1);
    }
    return str;
}

inline auto random_number() -> uint64_t { 
    auto r = rand();
    if (r == 100) r += 1; 
    return r;
}

inline auto replace_str(std::span<char> str, size_t len) {
    std::string chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    int pos;
    auto it = str.begin();
    while(len--) {
        pos = (rand() % chars.size());
        *it = chars.substr(pos,1)[0];
        it++;
    }
}


inline auto temp_filename(std::string tmpl = "/tmp/dsr_testfile_XXXXXX.json", int len = 5) -> std::string {

	size_t tmpl_len = tmpl.size();

    if (tmpl_len < 6 || len > tmpl_len - 6 || tmpl.substr(tmpl_len - len - 6, 6) != "XXXXXX") {
        return tmpl;
    }

    std::span<char> substr (tmpl.begin() + tmpl_len - len - 6, tmpl.begin() + tmpl_len - len);
    replace_str(substr, 6);

    return tmpl;
}


inline auto make_empty_config_file() -> std::string {

    auto filename = temp_filename();
    std::ofstream out_str(filename);
    
    out_str << R"EOF(
{
    "DSRModel": {
        "symbols": {
                "100": {
                "attribute": {
                    "level": {
                        "type": 1,
                        "value": 0
                    },
                },
                "id": "100",
                "links": [],
                "name": "root",
                "type": "root"
            },
        }
    }
})EOF" << std::endl;

    out_str.close();

    return filename;
}


inline auto make_edge_config_file() -> std::string {

    auto filename = temp_filename();
    std::ofstream out_str(filename);
    
    out_str << R"EOF(
{
    "DSRModel": {
        "symbols": {
                "100": {
                "attribute": {
                    "color": {
                        "type": 0,
                        "value": "SeaGreen"
                    },
                    "level": {
                        "type": 1,
                        "value": 0
                    },
                    "pos_x": {
                        "type": 2,
                        "value": 0
                    },
                    "pos_y": {
                        "type": 2,
                        "value": 0
                    }
                },
                "id": "100",
                "links": [
                    {
                        "dst": "150",
                        "label": "RT",
                        "linkAttribute": {
                            "rt_rotation_euler_xyz": {
                                "type": 3,
                                "value": [
                                    0,
                                    0,
                                    0
                                ]
                            },
                            "rt_translation": {
                                "type": 3,
                                "value": [
                                    0,
                                    0,
                                    0
                                ]
                            }
                        },
                        "src": "100"
                    }
                ],
                "name": "root",
                "type": "root"
            },
            "150": {
                "attribute": {
                    "OuterRegionBottom": {
                        "type": 1,
                        "value": 2500
                    },
                    "OuterRegionLeft": {
                        "type": 1,
                        "value": -5000
                    },
                    "OuterRegionRight": {
                        "type": 1,
                        "value": 5000
                    },
                    "OuterRegionTop": {
                        "type": 1,
                        "value": -2500
                    },
                    "color": {
                        "type": 0,
                        "value": "SeaGreen"
                    },
                    "level": {
                        "type": 1,
                        "value": 1
                    },
                    "parent": {
                        "type": 7,
                        "value": "100"
                    },
                    "pos_x": {
                        "type": 2,
                        "value": 0
                    },
                    "pos_y": {
                        "type": 2,
                        "value": 50
                    }
                },
                "id": "150",
                "links": [
                    {
                        "dst": "200",
                        "label": "RT",
                        "linkAttribute": {
                            "rt_rotation_euler_xyz": {
                                "type": 3,
                                "value": [
                                    0,
                                    0,
                                    0
                                ]
                            },
                            "rt_translation": {
                                "type": 3,
                                "value": [
                                    0,
                                    0,
                                    0
                                ]
                            }
                        },
                        "src": "150"
                    }
                ],
                "name": "room",
                "type": "room"
            },
            "200": {
                "attribute": {
                    "base_target_x": {
                        "type": 2,
                        "value": 0
                    },
                    "base_target_y": {
                        "type": 2,
                        "value": 0
                    },
                    "color": {
                        "type": 0,
                        "value": "Blue"
                    },
                    "level": {
                        "type": 1,
                        "value": 2
                    },
                    "parent": {
                        "type": 7,
                        "value": "150"
                    },
                    "port": {
                        "type": 1,
                        "value": 12238
                    },
                    "pos_x": {
                        "type": 2,
                        "value": 0
                    },
                    "pos_y": {
                        "type": 2,
                        "value": 100
                    },
                    "ref_adv_speed": {
                        "type": 2,
                        "value": 0
                    },
                    "ref_rot_speed": {
                        "type": 2,
                        "value": 0
                    },
                    "ref_side_speed": {
                        "type": 2,
                        "value": 0
                    },
                    "robot_local_linear_velocity": {
                        "type": 3,
                        "value": [
                            0.05445510149002075,
                            3.0953281778470884e-41,
                            0
                        ]
                    }
                },
                "id": "200",
                "links": [],
                "name": "Shadow",
                "type": "omnirobot"
            }
        }
    }
})EOF" << std::endl;

    out_str.close();

    return filename;
}
