/** biome-ignore-all lint/suspicious/noAssignInExpressions: it's fine :) */
import { pb } from "@/lib/api"
import { $recoveryChannels, $recoveryModules } from "@/lib/stores"
import type { RecoveryChannelRecord, RecoveryModuleRecord } from "@/types"

const MODULES = pb.collection<RecoveryModuleRecord>("recovery_modules")
const CHANNELS = pb.collection<RecoveryChannelRecord>("recovery_channels")

/**
 * Client-side mirror of isRecoveryModuleOnline (internal/hub/api.go) and
 * moduleIsLive (internal/hub/systems/recovery_prober.go): a module is
 * online when it pinged within max(90s, 3x its configured ping interval).
 */
export function isModuleOnline(mod: RecoveryModuleRecord | undefined): boolean {
	if (!mod?.last_ping) return false
	const staleAfterMs = Math.max(90_000, 3 * (mod.ping_interval_seconds || 30) * 1000)
	return Date.now() - new Date(mod.last_ping).getTime() < staleAfterMs
}

// biome-ignore lint/suspicious/noConfusingVoidType: typescript rocks
let unsubModules: (() => void) | undefined | void
// biome-ignore lint/suspicious/noConfusingVoidType: typescript rocks
let unsubChannels: (() => void) | undefined | void

function setModule(rec: RecoveryModuleRecord) {
	$recoveryModules.setKey(rec.id, rec)
}
function removeModule(rec: RecoveryModuleRecord) {
	$recoveryModules.setKey(rec.id, undefined as unknown as RecoveryModuleRecord)
}
function setChannel(rec: RecoveryChannelRecord) {
	$recoveryChannels.setKey(rec.id, rec)
}
function removeChannel(rec: RecoveryChannelRecord) {
	$recoveryChannels.setKey(rec.id, undefined as unknown as RecoveryChannelRecord)
}

const moduleActionFns: Record<string, (rec: RecoveryModuleRecord) => void> = {
	create: setModule,
	update: setModule,
	delete: removeModule,
}
const channelActionFns: Record<string, (rec: RecoveryChannelRecord) => void> = {
	create: setChannel,
	update: setChannel,
	delete: removeChannel,
}

/** Fetch current recovery modules/channels from their collections */
export async function refresh() {
	try {
		const [modules, channels] = await Promise.all([MODULES.getFullList({ sort: "+name" }), CHANNELS.getFullList()])
		for (const rec of modules) setModule(rec)
		for (const rec of channels) setChannel(rec)
	} catch (error) {
		console.error("Failed to refresh recovery modules:", error)
	}
}

/** Subscribe to real-time recovery module/channel updates */
export async function subscribe() {
	try {
		unsubModules = await MODULES.subscribe("*", ({ action, record }) => moduleActionFns[action]?.(record))
		unsubChannels = await CHANNELS.subscribe("*", ({ action, record }) => channelActionFns[action]?.(record))
	} catch (error) {
		console.error("Failed to subscribe to recovery collections:", error)
	}
}

/** Unsubscribe from real-time recovery updates */
export const unsubscribe = () => {
	unsubModules = unsubModules?.()
	unsubChannels = unsubChannels?.()
}
