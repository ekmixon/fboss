#!/usr/bin/env python3
#
#  Copyright (c) 2004-present, Facebook, Inc.
#  All rights reserved.
#
#  This source code is licensed under the BSD-style license found in the
#  LICENSE file in the root directory of this source tree. An additional grant
#  of patent rights can be found in the PATENTS file in the same directory.
#
import ipaddress
import sys

import click
from fboss.cli.commands import (
    acl,
    agent,
    aggregate_port,
    arp,
    commands as cmds,
    info,
    interface,
    ip,
    l2,
    list_hw_objects,
    lldp,
    ndp,
    nic,
    port,
    route,
)
from fboss.cli.commands.commands import FlushType
from fboss.cli.utils.click_utils import AliasedGroup
from fboss.cli.utils.utils import KEYWORD_CONFIG_RELOAD, KEYWORD_CONFIG_SHOW
from fboss.fb_thrift_clients import FbossAgentClient
from neteng.fboss.ctrl.ttypes import HwObjectType, PortLedExternalState, PrbsComponent
from neteng.fboss.ttypes import FbossBaseError
from thrift.Thrift import TApplicationException
from thrift.transport.TTransport import TTransportException


# Main Fboss py dir
sys.path.insert(1, "../../")

# Common dirs
sys.path.insert(2, "../../../agent/if/gen-py")


DEFAULT_CLIENTID = 1


class CliOptions(object):
    """Object for holding CLI state information"""

    def __init__(self, hostname, file, port, timeout):
        self.hostname = hostname
        self.port = port
        self.timeout = timeout
        self.snapshot_file = file


class ArpCli(object):
    """ARP Cli sub-commands"""

    def __init__(self):
        self.arp.add_command(self._table, name="table")
        self.arp.add_command(self._flush, name="flush")

    @click.group(cls=AliasedGroup)
    def arp():
        """Show ARP Information"""
        pass

    @click.command()
    @click.pass_obj
    def _table(self):
        """Show the ARP table"""
        arp.ArpTableCmd(self).run()

    @click.command()
    @click.option(
        "-V",
        "--vlan",
        type=int,
        default=0,
        help="Only flush the IP from the specified VLAN",
    )
    @click.argument("ip")
    @click.pass_obj
    def _flush(self, ip, vlan):
        """Flush an ARP entry by [IP] or [subnet] or flush [all]"""
        if ip == "all":
            ip = "0.0.0.0/0"
        cmds.NeighborFlushSubnetCmd(self).run(
            FlushType.arp, ipaddress.IPv4Network(ip), vlan
        )


class AggregatePortCli(object):
    """Aggregate Port sub-commands"""

    @click.command()
    @click.argument("port", required=False, default="")
    @click.pass_obj
    def aggregate_port(self, port):
        """Show aggregate port information; Outputs a list of
        aggregate port and the subports that are part of the
        aggregate port."""
        aggregate_port.AggregatePortCmd(self).run(port)


class NicCli(object):
    """NIC Cli sub-commands"""

    def __init__(self):
        self.nic.add_command(self._vendor, name="vendor")

    @click.group(cls=AliasedGroup)
    def nic():
        """Show NIC Information on connected hosts"""
        pass

    @click.command()
    @click.option("--detail", is_flag=True, help="Display detailed port status")
    @click.option("--verbose", is_flag=True, help="Display detailed port status")
    @click.pass_obj
    def _vendor(self, detail, verbose):
        """Show NIC vendor information on hosts"""
        nic.NicCmd(self).run(detail, verbose)


class IpCli(object):
    """IP sub-commands"""

    @click.command()
    @click.option(
        "-i",
        "--interface",
        type=int,
        required=True,
        help="Show Ip address of the interface",
    )
    @click.pass_obj
    def ip(self, interface):
        """Show IP information for an interface"""
        ip.IpCmd(self).run(interface)


class InterfaceCli(object):
    """Interface sub-commands"""

    def __init__(self):
        self.interface.add_command(self._show, name="show")
        self.interface.add_command(self._summary, name="summary")

    @click.group(cls=AliasedGroup)
    def interface():
        """Show Interface Information"""
        pass

    @click.command()
    @click.argument("interfaces", type=int, nargs=-1)
    @click.pass_obj
    def _show(self, interfaces):
        """Show interface information for Interface(s); Outputs a list of
        interfaces on host if no interfaces are specified"""
        interface.InterfaceShowCmd(self).run(interfaces)

    @click.command()
    @click.pass_obj
    def _summary(self):
        """Show interface summary"""
        interface.InterfaceSummaryCmd(self).run()


class L2Cli(object):
    """Show L2 sub-commands"""

    def __init__(self):
        self.l2.add_command(self._table, name="table")

    @click.group(cls=AliasedGroup)
    def l2():
        """Show L2 information"""
        pass

    @click.command()
    @click.pass_obj
    def _table(self):
        """Show the L2 table"""
        l2.L2TableCmd(self).run()


class LldpCli(object):
    """Lldp sub-commands"""

    @click.command()
    @click.option(
        "-p",
        "--port",
        type=int,
        default=None,
        help="Show only neighbors for the specified lldp port",
    )
    @click.option(
        "-v",
        "--verbose",
        count=True,
        help="Level of verbosity indicated by count, i.e -vvv",
    )
    @click.pass_obj
    def lldp(self, port, verbose):
        """Show LLDP neighbors"""
        lldp.LldpCmd(self).run(port, verbose)


class NdpCli(object):
    """Show NDP sub-commands"""

    def __init__(self):
        self.ndp.add_command(self._table, name="table")
        self.ndp.add_command(self._flush, name="flush")

    @click.group(cls=AliasedGroup)
    def ndp():
        """Show NDP information"""
        pass

    @click.command()
    @click.pass_obj
    def _table(self):
        """Show the NDP table"""
        ndp.NdpTableCmd(self).run()

    @click.command()
    @click.option(
        "-V",
        "--vlan",
        type=int,
        default=0,
        help="Only flush the IP from the specified VLAN",
    )
    @click.argument("ip")
    @click.pass_obj
    def _flush(self, ip, vlan):
        """Flush an NDP entry by [IP] or [subnet] or flush [all]"""
        if ip == "all":
            ip = "::/0"
        cmds.NeighborFlushSubnetCmd(self).run(
            FlushType.ndp, ipaddress.IPv6Network(ip), vlan
        )


class PortType(click.ParamType):
    port_info_map = None

    def convert(self, value, param, ctx):
        try:
            if self.port_info_map is None:
                with FbossAgentClient(ctx.obj.hostname) as client:
                    self.port_info_map = client.getAllPortInfo()
            if value.isdigit():
                port = self.port_info_map[int(value)]
                return port.portId
            for port_id, port_info in self.port_info_map.items():
                if port_info.name == value:
                    return port_id
            raise ValueError("No port found with that name")

        except (ValueError, KeyError):
            self.fail(f"{value} is not a valid Port", param, ctx)


class Stats(object):
    """Port stats sub-commands"""

    def __init__(self):
        self.stats.add_command(self._show, name="show")
        self.stats.add_command(self._clear, name="clear")

    @click.group(cls=AliasedGroup)
    def stats():
        """Show/clear Port stats"""
        pass

    @click.command()
    @click.argument("ports", nargs=-1, type=PortType())
    @click.option(
        "--detail", is_flag=True, help="Display detailed port stats with lldp"
    )
    @click.pass_obj
    def _show(self, detail, ports):
        """Show port statistics"""
        port.PortStatsCmd(self).run(detail, ports)

    @click.command()
    @click.argument("ports", nargs=-1, type=PortType())
    @click.pass_obj
    def _clear(self, ports):
        """Clear stats"""
        port.PortStatsClearCmd(self).run(ports)


class PortState(object):
    """Port state sub-commands"""

    def __init__(self):
        self.state.add_command(self._show, name="show")
        self.state.add_command(self._enable, name="enable")
        self.state.add_command(self._disable, name="disable")
        self.state.add_command(self._flap, name="flap")
        self.state.add_command(self._set_led, name="set_led")

    @click.group(cls=AliasedGroup)  # noqa: B902
    def state():
        """Port state commands"""
        pass

    @click.command()
    @click.argument("ports", nargs=-1, type=PortType())
    @click.option("--all", is_flag=True, help="Display Disabled ports")
    @click.pass_obj
    def _show(self, ports, all):    # noqa: B902
        """Show port state for given [port(s)]"""
        port.PortStatusCmd(self).run(
            detail=False, ports=ports, verbose=False, internal=True, all=all
        )

    @click.command()
    @click.argument("ports", nargs=-1, type=PortType())
    @click.pass_obj
    def _enable(self, ports):    # noqa: B902
        """Enable port state for given [port(s)]"""
        port.PortSetStatusCmd(self).run(ports, True)

    @click.command()
    @click.argument("ports", nargs=-1, type=PortType())
    @click.pass_obj
    def _disable(self, ports):    # noqa: B902
        """Disable port state for given [port(s)]"""
        port.PortSetStatusCmd(self).run(ports, False)

    @click.command()
    @click.argument("ports", nargs=-1, type=PortType())
    @click.option("--all", is_flag=True, help="Flap all Present but Down ports")
    @click.pass_obj
    def _flap(self, ports, all):    # noqa: B902
        """Flap port state for given [port(s)]"""
        port.PortFlapCmd(self).run(ports, all)

    @click.command()
    @click.argument("ports", nargs=-1, type=PortType())
    @click.option("--internal", is_flag=True, help="LED will show port state")
    @click.option("--on", is_flag=True, help="LED will be permanently ON")
    @click.option("--off", is_flag=True, help="LED will be permanently OFF")
    @click.pass_obj
    def _set_led(self, ports, internal, on, off):    # noqa: B902
        """Set LED light state for given [port(s)]"""
        value = PortLedExternalState.NONE
        if on:
            value = PortLedExternalState.EXTERNAL_FORCE_ON
        if off:
            value = PortLedExternalState.EXTERNAL_FORCE_OFF

        port.PortSetLedCmd(self).run(ports, value)


class PrbsContext(CliOptions):  # noqa: B903
    def __init__(self, cli_opts, component):
        self.component = component
        super(PrbsContext, self).__init__(
            cli_opts.hostname, cli_opts.snapshot_file, cli_opts.port, cli_opts.timeout
        )


class PortPrbsCli(object):
    """Port prbs sub-commands"""

    def __init__(self):
        self.prbs.add_command(self.asic, name="asic")
        self.prbs.add_command(self.gearbox, name="gearbox")

        self.gearbox.add_command(self.system, name="system")
        self.gearbox.add_command(self.line, name="line")

        self.system.add_command(self._enable, name="enable")
        self.system.add_command(self._disable, name="disable")
        self.system.add_command(self._stats, name="stats")
        self.system.add_command(self._clear, name="clear")

        self.line.add_command(self._enable, name="enable")
        self.line.add_command(self._disable, name="disable")
        self.line.add_command(self._stats, name="stats")
        self.line.add_command(self._clear, name="clear")

        self.asic.add_command(self._enable, name="enable")
        self.asic.add_command(self._disable, name="disable")
        self.asic.add_command(self._stats, name="stats")
        self.asic.add_command(self._clear, name="clear")

    @click.group(cls=AliasedGroup)  # noqa: B902
    def prbs():
        """Port prbs commands"""
        pass

    @click.group(cls=AliasedGroup)  # noqa: B902
    def gearbox():
        """Port prbs gearbox commands"""
        pass

    @click.group(cls=AliasedGroup)  # noqa: B902
    @click.pass_context
    def system(self):    # noqa: B902
        """Port prbs gearbox system commands"""
        self.obj = PrbsContext(self.obj, PrbsComponent.GB_SYSTEM)

    @click.group(cls=AliasedGroup)  # noqa: B902
    @click.pass_context
    def line(self):    # noqa: B902
        """Port prbs gearbox system commands"""
        self.obj = PrbsContext(self.obj, PrbsComponent.GB_LINE)

    @click.group(cls=AliasedGroup)  # noqa: B902
    @click.pass_context
    def asic(self):    # noqa: B902
        """Port prbs asic commands"""
        self.obj = PrbsContext(self.obj, PrbsComponent.ASIC)

    @click.command()
    @click.argument("ports", nargs=-1, type=PortType())
    @click.option(
        "-p",
        type=int,
        default=31,
        help="The polynomial to be used for PRBS (default: 31)",
    )
    @click.pass_obj
    def _enable(self, ports, p):    # noqa: B902
        """Enable prbs for given [port(s)]"""
        port.PortPrbsCmd(self, self.component, ports).set_prbs(True, p)

    @click.command()
    @click.argument("ports", nargs=-1, type=PortType())
    @click.pass_obj
    def _disable(self, ports):    # noqa: B902
        """Disable prbs for given [port(s)]"""
        port.PortPrbsCmd(self, self.component, ports).set_prbs(False)

    @click.command()
    @click.argument("ports", nargs=-1, type=PortType())
    @click.pass_obj
    def _stats(self, ports):    # noqa: B902
        """Get stats of prbs for given [port(s)]"""

        port.PortPrbsCmd(self, self.component, ports).get_prbs_stats()

    @click.command()
    @click.argument("ports", nargs=-1, type=PortType())
    @click.pass_obj
    def _clear(self, ports):    # noqa: B902
        """Clear stats of prbs for given [port(s)]"""
        port.PortPrbsCmd(self, self.component, ports).clear_prbs_stats()


class PortTransceiver(object):
    """Port transceiver sub-commands"""

    def __init__(self):
        self.transceiver.add_command(self._transceiver, name="show")

    @click.group(cls=AliasedGroup)  # noqa: B902
    def transceiver():
        """Port transceiver commands"""
        pass

    @click.command()
    @click.argument("ports", nargs=-1, type=PortType())
    @click.pass_obj
    def _transceiver(self, ports):    # noqa: B902
        """Show port transceiver for given [port(s)]"""
        port.PortStatusCmd(self).run(
            detail=False, ports=ports, verbose=True, internal=False, all=all
        )


class PortCli(object):
    """Port sub-commands"""

    def __init__(self):
        self.port.add_command(self._details, name="details")
        self.port.add_command(self._description, name="description")

        self.port.add_command(PortState().state)
        self.port.add_command(PortTransceiver().transceiver)
        self.port.add_command(Stats().stats)
        self.port.add_command(PortPrbsCli().prbs)

    @click.group(cls=AliasedGroup)
    def port():
        """Show port information"""
        pass

    @click.command()
    @click.argument("ports", nargs=-1, type=PortType())
    @click.pass_obj
    def _details(self, ports):
        """Show port details for given [port(s)]"""
        port.PortDetailsCmd(self).run(ports)

    @click.command()
    @click.argument("ports", nargs=-1, type=PortType())
    @click.pass_obj
    def _description(self, ports):
        """Show port description for given [port(s)]"""
        port.PortDescriptionCmd(self).run(ports)


class ProductInfoCli(object):
    """Product Info sub-commands"""

    @click.command()
    @click.option(
        "-d", "--detail", is_flag=True, help="Display detailed product information"
    )
    @click.pass_obj
    def product(self, detail):
        """Show product information"""
        info.ProductInfoCmd(self).run(detail)


class RouteCli(object):
    """Route sub-commands"""

    def __init__(self):
        self.route.add_command(self._ip, name="ip")
        self.route.add_command(self._table, name="table")
        self.route.add_command(self._details, name="details")
        self.route.add_command(self._add, name="add")
        self.route.add_command(self._delete, name="delete")
        self.route.add_command(self._flush, name="flush")
        self.route.add_command(self._summary, name="summary")

    @click.group(cls=AliasedGroup)
    def route():
        """Show route information"""
        pass

    @click.command()
    @click.argument("ip")
    @click.option(
        "-v",
        "--vrf",
        type=int,
        default=0,
        help="Show Route to the IP address in the vrf",
    )
    @click.pass_obj
    def _ip(self, ip, vrf):
        """Show the route to a specific IP"""
        route.RouteIpCmd(self).run(ip, vrf)

    @click.command()
    @click.option(
        "--client-id",
        type=int,
        default=None,
        help="If pass, show all routes programmed by certain client",
    )
    @click.option("-4", "--ipv4", is_flag=True, default=False, help="Show IPv4 routes")
    @click.option("-6", "--ipv6", is_flag=True, default=False, help="Show IPv6 routes")
    @click.argument("prefix", nargs=-1, type=str)
    @click.pass_obj
    def _table(self, client_id, ipv4, ipv6, prefix):    # noqa: B902
        """Show the route table"""
        route.RouteTableCmd(self).run(client_id, ipv4, ipv6, prefix)

    @click.command()
    @click.option("-4", "--ipv4", is_flag=True, default=False, help="Show IPv4 routes")
    @click.option("-6", "--ipv6", is_flag=True, default=False, help="Show IPv6 routes")
    @click.argument("prefix", nargs=-1, type=str)
    @click.pass_obj
    def _details(self, ipv4, ipv6, prefix):    # noqa: B902
        """Show details of the route table"""
        route.RouteTableDetailsCmd(self).run(ipv4, ipv6, prefix)

    @click.command()
    @click.argument("prefix", nargs=1, required=True)
    @click.argument("nexthop", nargs=-1, required=True)
    @click.option(
        "-c",
        "--client-id",
        type=int,
        default=DEFAULT_CLIENTID,
        help="The client ID used to manipulate the routes",
    )
    @click.option(
        "-d",
        "--admin-distance",
        type=int,
        default=None,
        help="DIRECTLY_CONNECTED=0, STATIC_ROUTE=1, "
        "EBGP=20, IBGP=200, "
        "MAX_ADMIN_DISTANCE=255",
    )
    @click.pass_obj
    def _add(self, client_id, admin_distance, prefix, nexthop):
        """
        Add a new route or change an existing route

        PREFIX - The route prefix, i.e. "1.1.1.0/24" or "2001::0/64\n
        NEXTHOP - The nexthops of the route, i.e "10.1.1.1" or "2002::1"
        """
        route.RouteAddCmd(self).run(client_id, admin_distance, prefix, nexthop)

    @click.command()
    @click.option(
        "-c",
        "--client-id",
        type=int,
        default=DEFAULT_CLIENTID,
        help="The client ID used to manipulate the routes",
    )
    @click.argument("prefix")
    @click.pass_obj
    def _delete(self, client_id, prefix):
        """
        Delete an existing route

        PREFIX - The route prefix, i.e. "1.1.1.0/24" or "2001::0/64"
        """
        route.RouteDelCmd(self).run(client_id, prefix)

    @click.command()
    @click.option(
        "-c",
        "--client-id",
        type=int,
        default=DEFAULT_CLIENTID,
        help="The client ID used to manipulate the routes",
    )
    @click.pass_obj
    def _flush(self, client_id):
        """Flush all existing non-interface routes"""
        route.RouteFlushCmd(self).run(client_id)

    @click.command()
    @click.pass_obj
    def _summary(self):
        """Print a summary of routing tables"""
        route.RouteTableSummaryCmd(self).run()


class VerbosityCli(object):
    """Change the verbosity of the fboss agent"""

    @click.command(name="verbosity")
    @click.argument("verbosity")
    @click.pass_obj
    def set(self, verbosity):
        cmds.VerbosityCmd(self).run(verbosity)


class AgentConfig(object):
    """Agent config sub-commands"""

    def __init__(self):
        self.config.add_command(self._show, name="show")
        self.config.add_command(self._reload, name="reload")

    @click.group(cls=AliasedGroup)  # noqa: B902
    def config():
        """Agent config commands"""
        pass

    @click.command()
    @click.option(
        "--json", is_flag=True, default=False, help="Show configuration in JSON format"
    )
    @click.pass_obj
    def _show(self, json):    # noqa: B902
        """Show running config"""
        agent.AgentConfigCmd(self).run(KEYWORD_CONFIG_SHOW, json)

    @click.command()
    @click.pass_obj
    def _reload(self):    # noqa: B902
        """Reload agent configuration file"""
        agent.AgentConfigCmd(self).run(KEYWORD_CONFIG_RELOAD)


class AgentCli(object):
    """Agent sub-commands"""

    def __init__(self):
        self.agent.add_command(AgentConfig().config)

    @click.group(cls=AliasedGroup)  # noqa: B902
    def agent():
        """agent commands"""
        pass


class AclCli(object):
    """Show Acl sub-commands"""

    def __init__(self):
        self.acl.add_command(self._table, name="table")

    @click.group(cls=AliasedGroup)
    def acl():
        """Show Acl information"""
        pass

    @click.command()
    @click.pass_obj
    def _table(self):
        """Show the Acl table"""
        acl.AclTableCmd(self).run()


class ListHwObjectsCli(object):
    """Show list Hw obects sub-commands"""

    def __init__(self):
        self.list_hw_objects.add_command(self._list, name="list")

    @click.group(cls=AliasedGroup)
    def list_hw_objects():
        """List HW objecttsn"""
        pass

    @click.command()
    @click.option(
        "-o",
        "--hw-object",
        multiple=True,
        default=None,
        type=click.Choice(sorted(HwObjectType()._NAMES_TO_VALUES.keys())),
    )
    @click.option(
        "-c",
        "--cached",
        default=False,
        type=bool,
        is_flag=True,
        help="Use objects cached in memory rather than retrieving from HW",
    )
    @click.pass_obj
    def _list(self, hw_object, cached):
        """List Hw objects"""
        hw_obj_types = [
            HwObjectType()._NAMES_TO_VALUES[_hw_obj_type] for _hw_obj_type in hw_object
        ]
        self.options["hw_obj_types"] = hw_obj_types
        list_hw_objects.ListHwObjectsCmd(self).run(hw_obj_types, cached)


# -- Main Command Group -- #
@click.group(cls=AliasedGroup)
@click.option(
    "--hostname",
    "-H",
    default="::1",
    type=str,
    help="Host to connect to (default = ::1)",
)
@click.option("--file", "-F", default=None, type=str, help="Snapshot file to read from")
@click.option("--port", "-p", default=None, type=int, help="Thrift port to connect to")
@click.option(
    "--timeout", "-t", default=None, type=int, help="Thrift client timeout in seconds"
)
@click.pass_context
def main(ctx, hostname, file, port, timeout):
    """Main CLI options, all options are passed to children via the context obj
    "ctx" and can be accessed accordingly"""
    ctx.obj = CliOptions(hostname, file, port, timeout)


def add_modules(main_func):
    """Add sub-commands to main"""

    main_func.add_command(ArpCli().arp)
    main_func.add_command(AggregatePortCli().aggregate_port)
    main_func.add_command(IpCli().ip)
    main_func.add_command(InterfaceCli().interface)
    main_func.add_command(L2Cli().l2)
    main_func.add_command(ListHwObjectsCli().list_hw_objects)
    main_func.add_command(LldpCli().lldp)
    main_func.add_command(NdpCli().ndp)
    main_func.add_command(NicCli().nic)
    main_func.add_command(PortCli().port)
    main_func.add_command(ProductInfoCli().product)
    main_func.add_command(RouteCli().route)
    main_func.add_command(VerbosityCli().set)
    main_func.add_command(AgentCli().agent)
    main_func.add_command(AclCli().acl)


if __name__ == "__main__":

    # -- Add sub-commands to "Main" -- #
    add_modules(main)

    try:
        main()
    except FbossBaseError as e:
        raise SystemExit(f"Fboss Error: {e}")
    except TApplicationException:
        raise SystemExit("Command not available on host")
    except TTransportException:
        raise SystemExit("Failed connecting to host")
