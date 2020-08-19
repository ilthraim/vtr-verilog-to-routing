#include "overuse_report.h"

#include <fstream>
#include "globals.h"
#include "vtr_log.h"

static void report_overused_ipin_opin(std::ostream& os, RRNodeId node_id);
static void report_overused_chanx_chany(std::ostream& os, RRNodeId node_id);
static void report_overused_source_sink(std::ostream& os, RRNodeId node_id);
static void report_congested_nets(std::ostream& os, const std::set<ClusterNetId>& congested_nets);

static void log_overused_nodes_header();
static void log_single_overused_node_status(int overuse_index, RRNodeId inode);

void log_overused_nodes_status(int max_logged_overused_rr_nodes) {
    const auto& device_ctx = g_vpr_ctx.device();
    const auto& route_ctx = g_vpr_ctx.routing();

    //Print overuse info header
    log_overused_nodes_header();

    //Print overuse info body
    int overuse_index = 0;
    for (size_t inode = 0; inode < device_ctx.rr_nodes.size(); inode++) {
        int overuse = route_ctx.rr_node_route_inf[inode].occ() - device_ctx.rr_nodes[inode].capacity();

        if (overuse > 0) {
            log_single_overused_node_status(overuse_index, RRNodeId(inode));
            ++overuse_index;

            //Reached the logging limit
            if (overuse_index >= max_logged_overused_rr_nodes) {
                return;
            }
        }
    }
}

void report_overused_nodes() {
    const auto& device_ctx = g_vpr_ctx.device();
    const auto& route_ctx = g_vpr_ctx.routing();

    //Generate overuse info lookup table
    std::map<RRNodeId, std::set<ClusterNetId>> nodes_to_nets_lookup;
    generate_overused_nodes_to_congested_net_lookup(nodes_to_nets_lookup);

    //Open the report file
    std::ofstream os("report_overused_nodes.rpt");
    os << "Overused nodes information report on the final failed routing attempt" << '\n';
    os << "Total number of overused nodes = " << nodes_to_nets_lookup.size() << '\n';

    size_t inode = 0;
    for (const auto& lookup_pair : nodes_to_nets_lookup) {
        const RRNodeId node_id = lookup_pair.first;
        const auto& congested_nets = lookup_pair.second;

        os << "************************************************\n\n"; //RR Node Separation line

        //Report Basic info
        os << "Overused RR node #" << inode << '\n';
        os << "Node id = " << size_t(node_id) << '\n';
        os << "Occupancy = " << route_ctx.rr_node_route_inf[size_t(node_id)].occ() << '\n';
        os << "Capacity = " << device_ctx.rr_nodes.node_capacity(node_id) << "\n\n";

        //Report Selective info
        os << "Node type = " << device_ctx.rr_nodes.node_type_string(node_id) << '\n';

        auto node_type = device_ctx.rr_nodes.node_type(node_id);
        switch (node_type) {
            case IPIN:
            case OPIN:
                report_overused_ipin_opin(os, node_id);
                break;
            case CHANX:
            case CHANY:
                report_overused_chanx_chany(os, node_id);
                break;
            case SOURCE:
            case SINK:
                report_overused_source_sink(os, node_id);
                break;

            default:
                break;
        }

        os << "-----------------------------\n"; //Node/net info separation line
        report_congested_nets(os, congested_nets);

        ++inode;
    }

    os.close();
}

void generate_overused_nodes_to_congested_net_lookup(std::map<RRNodeId, std::set<ClusterNetId>>& nodes_to_nets_lookup) {
    const auto& device_ctx = g_vpr_ctx.device();
    const auto& route_ctx = g_vpr_ctx.routing();
    const auto& cluster_ctx = g_vpr_ctx.clustering();

    //Create overused nodes to congested nets look up by
    //traversing through the net trace backs linked lists
    for (ClusterNetId net_id : cluster_ctx.clb_nlist.nets()) {
        for (t_trace* tptr = route_ctx.trace[net_id].head; tptr != nullptr; tptr = tptr->next) {
            int inode = tptr->index;

            int overuse = route_ctx.rr_node_route_inf[inode].occ() - device_ctx.rr_nodes[inode].capacity();
            if (overuse > 0) {
                nodes_to_nets_lookup[RRNodeId(inode)].insert(net_id);
            }
        }
    }
}

static void report_overused_ipin_opin(std::ostream& os, RRNodeId node_id) {
    const auto& device_ctx = g_vpr_ctx.device();
    const auto& place_ctx = g_vpr_ctx.placement();

    auto grid_x = device_ctx.rr_nodes.node_xlow(node_id);
    auto grid_y = device_ctx.rr_nodes.node_ylow(node_id);
    VTR_ASSERT_MSG(
        grid_x == device_ctx.rr_nodes.node_xhigh(node_id) && grid_y == device_ctx.rr_nodes.node_yhigh(node_id),
        "Non-track RR node should not span across multiple grid blocks.");

    os << "Pin number = " << device_ctx.rr_nodes.node_pin_num(node_id) << '\n';
    os << "Side = " << device_ctx.rr_nodes.node_side_string(node_id) << "\n\n";

    //Add block type for IPINs/OPINs in overused rr-node report
    const auto& clb_nlist = g_vpr_ctx.clustering().clb_nlist;
    auto& grid_info = place_ctx.grid_blocks[grid_x][grid_y];
    auto& grid_blocks = grid_info.blocks;

    os << "Grid location: X = " << grid_x << ", Y = " << grid_y << '\n';
    os << "Number of blocks currently at this grid location = " << grid_info.usage << '\n';
    for (size_t iblock = 0; iblock < grid_blocks.size(); ++iblock) {
        ClusterBlockId block_id = grid_blocks[iblock];
        os << "Block #" << iblock << ": ";
        os << "Block name = " << clb_nlist.block_pb(block_id)->name << ", ";
        os << "Block type = " << clb_nlist.block_type(block_id)->name << '\n';
    }
}

static void report_overused_chanx_chany(std::ostream& os, RRNodeId node_id) {
    const auto& device_ctx = g_vpr_ctx.device();

    os << "Track number = " << device_ctx.rr_nodes.node_track_num(node_id) << '\n';
    os << "Direction = " << device_ctx.rr_nodes.node_direction_string(node_id) << "\n\n";

    os << "Grid location: " << '\n';
    os << "Xlow = " << device_ctx.rr_nodes.node_xlow(node_id) << ", ";
    os << "Ylow = " << device_ctx.rr_nodes.node_ylow(node_id) << '\n';
    os << "Xhigh = " << device_ctx.rr_nodes.node_xhigh(node_id) << ", ";
    os << "Yhigh = " << device_ctx.rr_nodes.node_yhigh(node_id) << '\n';
    os << "Resistance = " << device_ctx.rr_nodes.node_R(node_id) << '\n';
    os << "Capacitance = " << device_ctx.rr_nodes.node_C(node_id) << '\n';
}

static void report_overused_source_sink(std::ostream& os, RRNodeId node_id) {
    const auto& device_ctx = g_vpr_ctx.device();

    auto grid_x = device_ctx.rr_nodes.node_xlow(node_id);
    auto grid_y = device_ctx.rr_nodes.node_ylow(node_id);
    VTR_ASSERT_MSG(
        grid_x == device_ctx.rr_nodes.node_xhigh(node_id) && grid_y == device_ctx.rr_nodes.node_yhigh(node_id),
        "Non-track RR node should not span across multiple grid blocks.");

    os << "Class number = " << device_ctx.rr_nodes.node_class_num(node_id) << '\n';
    os << "Grid location: X = " << grid_x << ", Y = " << grid_y << '\n';
}

//Reported congested nets at a specific rr node
static void report_congested_nets(std::ostream& os, const std::set<ClusterNetId>& congested_nets) {
    const auto& clb_nlist = g_vpr_ctx.clustering().clb_nlist;
    os << "Number of nets passing through this RR node = " << congested_nets.size() << '\n';

    size_t inet = 0;
    for (ClusterNetId net_id : congested_nets) {
        ClusterBlockId block_id = clb_nlist.net_driver_block(net_id);
        os << "Net #" << inet << ": ";
        os << "Net ID = " << size_t(net_id) << ", ";
        os << "Net name = " << clb_nlist.net_name(net_id) << ", ";
        os << "Driving block name = " << clb_nlist.block_pb(block_id)->name << ", ";
        os << "Driving block type = " << clb_nlist.block_type(block_id)->name << '\n';
        ++inet;
    }
    os << '\n';
}

static void log_overused_nodes_header() {
    VTR_LOG("Routing Failure Diagnostics: Printing Overused Nodes Information\n");
    VTR_LOG("------ ------- ---------- --------- -------- ------------ ------- ------- ------- ------- ------- -------\n");
    VTR_LOG("   No.  NodeId  Occupancy  Capacity  RR Node    Direction    Side     PTC    Xlow    Ylow   Xhigh   Yhigh\n");
    VTR_LOG("                                        type                          NUM                                \n");
    VTR_LOG("------ ------- ---------- --------- -------- ------------ ------- ------- ------- ------- ------- -------\n");
}

static void log_single_overused_node_status(int overuse_index, RRNodeId node_id) {
    const auto& device_ctx = g_vpr_ctx.device();
    const auto& route_ctx = g_vpr_ctx.routing();

    //Determines if direction or side is available for printing
    auto node_type = device_ctx.rr_nodes.node_type(node_id);

    //Overuse #
    VTR_LOG("%6d", overuse_index);

    //Inode
    VTR_LOG(" %7d", size_t(node_id));

    //Occupancy
    VTR_LOG(" %10d", route_ctx.rr_node_route_inf[size_t(node_id)].occ());

    //Capacity
    VTR_LOG(" %9d", device_ctx.rr_nodes.node_capacity(node_id));

    //RR node type
    VTR_LOG(" %8s", device_ctx.rr_nodes.node_type_string(node_id));

    //Direction
    if (node_type == e_rr_type::CHANX || node_type == e_rr_type::CHANY) {
        VTR_LOG(" %12s", device_ctx.rr_nodes.node_direction_string(node_id));
    } else {
        VTR_LOG(" %12s", "N/A");
    }

    //Side
    if (node_type == e_rr_type::IPIN || node_type == e_rr_type::OPIN) {
        VTR_LOG(" %7s", device_ctx.rr_nodes.node_side_string(node_id));
    } else {
        VTR_LOG(" %7s", "N/A");
    }

    //PTC number
    VTR_LOG(" %7d", device_ctx.rr_nodes.node_ptc_num(node_id));

    //X_low
    VTR_LOG(" %7d", device_ctx.rr_nodes.node_xlow(node_id));

    //Y_low
    VTR_LOG(" %7d", device_ctx.rr_nodes.node_ylow(node_id));

    //X_high
    VTR_LOG(" %7d", device_ctx.rr_nodes.node_xhigh(node_id));

    //Y_high
    VTR_LOG(" %7d", device_ctx.rr_nodes.node_yhigh(node_id));

    VTR_LOG("\n");

    fflush(stdout);
}