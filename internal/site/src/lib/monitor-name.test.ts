import { expect, test } from "bun:test"
import type { NetworkMonitorRecord } from "@/types"
import { COMPARE_MONITOR_FIELDS, getMonitorDisplayName, NETWORK_MONITOR_FIELDS } from "./monitor-compare"

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

test("monitor list and compare field lists request the name", () => {
	expect(NETWORK_MONITOR_FIELDS.split(",")).toContain("name")
	expect(COMPARE_MONITOR_FIELDS.split(",")).toContain("name")
})
