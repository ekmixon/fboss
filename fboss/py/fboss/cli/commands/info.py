#!/usr/bin/env python3
#
#  Copyright (c) 2004-present, Facebook, Inc.
#  All rights reserved.
#
#  This source code is licensed under the BSD-style license found in the
#  LICENSE file in the root directory of this source tree. An additional grant
#  of patent rights can be found in the PATENTS file in the same directory.
#

from fboss.cli.commands import commands as cmds


class ProductInfoCmd(cmds.FbossCmd):
    def run(self, detail):
        with self._create_agent_client() as client:
            resp = client.getProductInfo()
        if detail:
            self._print_product_details(resp)
        else:
            self._print_product_info(resp)

    def _print_product_info(self, productInfo):
        print(f"Product: {productInfo.product}")
        print(f"OEM: {productInfo.oem}")
        print(f"Serial: {productInfo.serial}")

    def _print_product_details(self, productInfo):
        print(f"Product: {productInfo.product}")
        print(f"OEM: {productInfo.oem}")
        print(f"Serial: {productInfo.serial}")
        print(f"Management MAC Address: {productInfo.mgmtMac}")
        print(f"BMC MAC Address: {productInfo.bmcMac}")
        print(f"Extended MAC Start: {productInfo.macRangeStart}")
        print(f"Extended MAC Size: {productInfo.macRangeSize}")
        print(f"Assembled At: {productInfo.assembledAt}")
        print(f"Product Asset Tag: {productInfo.assetTag}")
        print(f"Product Part Number: {productInfo.partNumber}")
        print(f"Product Production State: {productInfo.productionState}")
        print(f"Product Sub-Version: {productInfo.subVersion}")
        print(f"Product Version: {productInfo.productVersion}")
        print(f"System Assembly Part Number: {productInfo.systemPartNumber}")
        print(f"System Manufacturing Date: {productInfo.mfgDate}")
        print(f"PCB Manufacturer: {productInfo.pcbManufacturer}")
        print(f"Facebook PCBA Part Number: {productInfo.fbPcbaPartNumber}")
        print(f"Facebook PCB Part Number: {productInfo.fbPcbPartNumber}")
        print(f"ODM PCBA Part Number: {productInfo.odmPcbaPartNumber}")
        print(f"ODM PCBA Serial Number: {productInfo.odmPcbaSerial}")
        print(f"Version: {productInfo.version}")
