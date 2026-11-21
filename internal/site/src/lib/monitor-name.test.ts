import { expect, test } from "bun:test"
import type { NetworkMonitorRecord } from "@/types"
import { getMonitorDisplayName } from "./network-monitor-utils"

const mon = (extra: Partial<NetworkMonitorRecord>) =>
	({ id: "m", system: "s", target: "http://192.168.13.5:8123", protocol: "http", port: 0, ...extra }) as NetworkMonitorRecord

test("display name uses the name when set", () => {
	expect(getMonitorDisplayName(mon({ name: "Home Assistant" }))).toBe("Home Assistant")
})

test("display name falls back to the target when the name is empty or missing", () => {
	expect(getMonitorDisplayName(mon({ name: "" }))).toBe(getMonitorDisplayName(mon({})))
	expect(getMonitorDisplayName(mon({ name: "   " }))).toBe(getMonitorDisplayName(mon({})))
	expect(getMonitorDisplayName(mon({}))).not.toBe("")
})
