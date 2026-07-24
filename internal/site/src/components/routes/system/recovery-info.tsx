import { useEffect, useState } from "react"
import { t } from "@lingui/core/macro"
import { Trans } from "@lingui/react/macro"
import type { ClientResponseError } from "pocketbase"
import { pb } from "@/lib/api"
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card"
import { Separator } from "@/components/ui/separator"
import { Button } from "@/components/ui/button"
import { Switch } from "@/components/ui/switch"
import { toast } from "@/components/ui/use-toast"
import { ShieldCheck, ShieldAlert, AlertTriangle, LoaderCircle, Power, RefreshCw } from "lucide-react"

interface RecoveryModuleSummary {
	id: string
	name: string
	ip_address?: string
	status: string
}

// RecoveryModuleDetail is the enriched module payload from
// GET /api/beszel/recovery/module?id=X (buildRecoveryModuleResponse in
// internal/hub/api.go) - the hub-computed sync_status/health_score aren't
// present on the raw recovery_channels->module expand, so this is fetched
// separately once we know which module this channel belongs to.
interface RecoveryModuleDetail {
	id: string
	sync_status: string
	health_score: number
	health_reasons: string[]
	pending_esp_change: boolean
}

interface RecoveryStats {
	recovery_count: number
	last_recovery: string | null
}

interface RecoveryChannel {
	id: string
	module: string
	channel_number: number
	system: string
	host_ip: string
	maintenance: boolean
	wol_enabled: boolean
	auto_wol: boolean
	mac_address: string
	hardware_recovery_disabled: boolean
	expand?: {
		module?: RecoveryModuleSummary
	}
}

interface RecoveryInfoProps {
	systemId: string
}

type FetchState = "loading" | "configured" | "not_configured" | "error"

// Fields on recovery_channels that can be toggled directly from this card via
// a confirmed (not optimistic) update - the switch stays disabled while the
// request is in flight and only reflects the new value once the backend
// confirms it, since these settings directly affect physical recovery
// behavior.
type ToggleField = "wol_enabled" | "auto_wol" | "hardware_recovery_disabled"

export default function RecoveryInfo({ systemId }: RecoveryInfoProps) {
	const [fetchState, setFetchState] = useState<FetchState>("loading")
	const [channel, setChannel] = useState<RecoveryChannel | null>(null)
	const [moduleDetail, setModuleDetail] = useState<RecoveryModuleDetail | null>(null)
	const [stats, setStats] = useState<RecoveryStats | null>(null)
	const [savingField, setSavingField] = useState<ToggleField | null>(null)
	const [events, setEvents] = useState<any[]>([])
	const [eventsLoading, setEventsLoading] = useState(true)
	const [isWaking, setIsWaking] = useState(false)
	const [isTriggering, setIsTriggering] = useState(false)

	useEffect(() => {
		let isMounted = true
		async function fetchChannel() {
			setFetchState("loading")
			try {
				const rec = await pb.collection("recovery_channels").getFirstListItem<RecoveryChannel>(`system="${systemId}"`, {
					expand: "module",
				})
				if (isMounted) {
					setChannel(rec)
					setFetchState("configured")
				}
			} catch (e) {
				if (!isMounted) return
				// A 404 from getFirstListItem just means no recovery_channels
				// record exists for this system - a normal, expected state,
				// not a failure. Any other error (network, permission) is a
				// real error and should be shown as such.
				const status = (e as ClientResponseError)?.status
				setFetchState(status === 404 ? "not_configured" : "error")
			}
		}
		fetchChannel()
		return () => {
			isMounted = false
		}
	}, [systemId])

	useEffect(() => {
		const moduleId = channel?.expand?.module?.id
		if (!moduleId) {
			setModuleDetail(null)
			return
		}
		let isMounted = true
		async function fetchModuleDetail() {
			try {
				const res = await pb.send<RecoveryModuleDetail>("/api/beszel/recovery/module", {
					query: { id: moduleId },
				})
				if (isMounted) {
					setModuleDetail(res)
				}
			} catch (e) {
				console.error(e)
			}
		}
		fetchModuleDetail()
		return () => {
			isMounted = false
		}
	}, [channel?.expand?.module?.id])

	useEffect(() => {
		let isMounted = true
		async function fetchStats() {
			try {
				const res = await pb.send<RecoveryStats>("/api/beszel/recovery/stats", {
					query: { system: systemId },
				})
				if (isMounted) {
					setStats(res)
				}
			} catch (e) {
				console.error(e)
			}
		}
		fetchStats()
		return () => {
			isMounted = false
		}
	}, [systemId])

	useEffect(() => {
		let isMounted = true
		async function fetchEvents() {
			try {
				const res = await pb.send("/api/beszel/recovery/events", {
					query: { system: systemId },
				})
				// Guard against a non-array response (e.g. an SPA HTML fallback
				// from a route mismatch) so events.filter() below can't throw
				// and blank the whole page.
				if (isMounted) {
					setEvents(Array.isArray(res) ? res : [])
				}
			} catch (e) {
				console.error(e)
			} finally {
				if (isMounted) {
					setEventsLoading(false)
				}
			}
		}
		fetchEvents()
		return () => {
			isMounted = false
		}
	}, [systemId])

	async function updateChannelField(field: ToggleField, value: boolean) {
		if (!channel) return
		setSavingField(field)
		try {
			await pb.collection("recovery_channels").update(channel.id, { [field]: value })
			setChannel((prev) => (prev ? { ...prev, [field]: value } : prev))
		} catch (e) {
			toast({
				title: t`Update failed`,
				description: (e as Error).message,
				variant: "destructive",
			})
		} finally {
			setSavingField(null)
		}
	}

	async function refreshEvents() {
		try {
			const res = await pb.send("/api/beszel/recovery/events", {
				query: { system: systemId },
			})
			setEvents(Array.isArray(res) ? res : [])
		} catch {
			// non-fatal - the action's own toast already reported success/failure
		}
	}

	async function triggerManualWake() {
		setIsWaking(true)
		try {
			await pb.send("/api/beszel/recovery/wake", {
				method: "POST",
				query: { system: systemId },
			})
			toast({
				title: t`Wake-on-LAN magic packet sent`,
				description: t`The broadcast has been sent on UDP port 9.`,
			})
			await refreshEvents()
		} catch (e) {
			toast({
				title: t`WOL broadcast failed`,
				description: (e as Error).message,
				variant: "destructive",
			})
		} finally {
			setIsWaking(false)
		}
	}

	async function triggerManualRelay() {
		setIsTriggering(true)
		try {
			await pb.send("/api/beszel/recovery/relay", {
				method: "POST",
				query: { system: systemId },
			})
			toast({
				title: t`Physical relay reboot triggered`,
				description: t`The motherboard power button relay has been pressed.`,
			})
			await refreshEvents()
		} catch (e) {
			toast({
				title: t`Relay trigger failed`,
				description: (e as Error).message,
				variant: "destructive",
			})
		} finally {
			setIsTriggering(false)
		}
	}

	async function triggerManualShutdown() {
		setIsTriggering(true)
		try {
			await pb.send("/api/beszel/recovery/shutdown", {
				method: "POST",
				query: { system: systemId },
			})
			toast({
				title: t`Graceful shutdown triggered`,
				description: t`The motherboard power button was momentarily pressed.`,
			})
			await refreshEvents()
		} catch (e) {
			toast({
				title: t`Shutdown failed`,
				description: (e as Error).message,
				variant: "destructive",
			})
		} finally {
			setIsTriggering(false)
		}
	}

	async function triggerManualForceRestart() {
		if (!confirm(t`Are you sure you want to force restart this server? This will cut power instantly.`)) return
		setIsTriggering(true)
		try {
			await pb.send("/api/beszel/recovery/force-restart", {
				method: "POST",
				query: { system: systemId },
			})
			toast({
				title: t`Force restart triggered`,
				description: t`The motherboard power button is being held for 8 seconds.`,
			})
			await refreshEvents()
		} catch (e) {
			toast({
				title: t`Restart failed`,
				description: (e as Error).message,
				variant: "destructive",
			})
		} finally {
			setIsTriggering(false)
		}
	}

	if (fetchState === "loading") {
		return (
			<div className="mt-4">
				<Card>
					<CardContent className="flex items-center justify-center h-24 text-muted-foreground text-sm">
						<LoaderCircle className="h-4 w-4 animate-spin mr-2" />
						<Trans>Loading recovery configuration...</Trans>
					</CardContent>
				</Card>
			</div>
		)
	}

	if (fetchState === "error") {
		return (
			<div className="mt-4">
				<Card>
					<CardContent className="flex items-center justify-center h-24 text-muted-foreground text-sm">
						<Trans>Failed to load recovery configuration.</Trans>
					</CardContent>
				</Card>
			</div>
		)
	}

	if (fetchState === "not_configured" || !channel) {
		return (
			<div className="mt-4">
				<Card>
					<CardContent className="flex items-center justify-center h-24 text-muted-foreground text-sm text-center">
						<Trans>Hardware recovery is not configured for this system.</Trans>
					</CardContent>
				</Card>
			</div>
		)
	}

	const hasWol = channel.wol_enabled
	const espModule = channel.expand?.module
	const hasEsp = !!channel.module && !!espModule
	const hasMaint = channel.maintenance
	const isEspOffline = espModule?.status === "offline"

	// Real hub-computed score when an ESP module is present; a simple two-tier
	// fallback (matching the ESP-less WOL-only case) otherwise.
	const healthScore = hasEsp && moduleDetail ? moduleDetail.health_score : hasMaint ? 80 : 100

	// A maintenance toggle is "requested" until the module is confirmed
	// online and synced - i.e. until we know the ESP has actually picked up
	// the change on a recent ping cycle - and "active" once that's confirmed.
	const maintenanceRequested =
		hasMaint && (isEspOffline || (hasEsp && moduleDetail?.sync_status !== "SYNCED"))
	const maintenanceActive = hasMaint && !maintenanceRequested

	let statusLabel = <Trans>HEALTHY</Trans>
	let statusColor = "text-green-500"
	let Icon = ShieldCheck

	if (maintenanceRequested) {
		statusLabel = <Trans>MAINTENANCE REQUESTED</Trans>
		statusColor = "text-yellow-500"
		Icon = AlertTriangle
	} else if (maintenanceActive) {
		statusLabel = <Trans>MAINTENANCE ACTIVE</Trans>
		statusColor = "text-yellow-500"
		Icon = AlertTriangle
	} else if (hasEsp && isEspOffline) {
		statusLabel = <Trans>DEGRADED</Trans>
		statusColor = "text-red-500"
		Icon = ShieldAlert
	} else if (hasEsp && (moduleDetail?.sync_status === "CONFLICT" || moduleDetail?.sync_status === "SYNC_ERROR")) {
		statusLabel = <Trans>SYNC ISSUE</Trans>
		statusColor = "text-red-500"
		Icon = ShieldAlert
	}

	const interventions = events.filter(
		(e) =>
			e.event === "WOL_SENT" ||
			e.event === "ESP_RELAY_SENT" ||
			e.event === "WOL_MANUAL_SENT" ||
			e.event === "RELAY_MANUAL_SENT"
	).length
	const successes = events.filter(
		(e) => e.event === "WOL_SUCCESS" || e.event === "RELAY_SUCCESS" || e.event === "FAST_VERIFY_RECOVERED"
	).length
	const successRate = interventions > 0 ? ((successes / interventions) * 100).toFixed(0) : "100"

	return (
		<div className="grid xl:grid-cols-2 gap-4 mt-4">
			<Card>
				<CardHeader className="pb-3">
					<CardTitle className="text-md font-semibold flex items-center gap-2">
						<Icon className={`size-5 ${statusColor}`} />
						<Trans>Recovery Protection</Trans>
					</CardTitle>
				</CardHeader>
				<CardContent className="space-y-4">
					<div className="flex justify-between items-center text-sm">
						<span className="text-muted-foreground">
							<Trans>Protection Status</Trans>
						</span>
						<span
							className={`font-semibold ${statusColor}`}
							title={moduleDetail?.health_reasons?.join(", ")}
						>
							{statusLabel} ({healthScore}%)
						</span>
					</div>
					<Separator />
					<div className="space-y-2 text-sm">
						<div className="flex justify-between items-center">
							<span className="text-muted-foreground">
								<Trans>Wake-on-LAN</Trans>
							</span>
							<Switch
								checked={hasWol}
								disabled={savingField === "wol_enabled"}
								onCheckedChange={(checked) => updateChannelField("wol_enabled", checked)}
							/>
						</div>
						{hasWol && (
							<>
								<div className="flex justify-between items-center pl-4 text-xs text-muted-foreground">
									<span>
										<Trans>Automatic WOL</Trans>
									</span>
									<Switch
										checked={channel.auto_wol}
										disabled={savingField === "auto_wol"}
										onCheckedChange={(checked) => updateChannelField("auto_wol", checked)}
									/>
								</div>
								<div className="flex justify-between pl-4 text-xs text-muted-foreground">
									<span>
										<Trans>MAC Address</Trans>
									</span>
									<span className="font-mono">{channel.mac_address || "N/A"}</span>
								</div>
								<div className="pt-2 pl-4">
									<Button size="sm" variant="outline" onClick={triggerManualWake} disabled={isWaking}>
										{isWaking ? <LoaderCircle className="h-3 w-3 animate-spin mr-1.5" /> : null}
										<Trans>Wake Server</Trans>
									</Button>
								</div>
							</>
						)}
					</div>
					<Separator />
					<div className="space-y-2 text-sm">
						<div className="flex justify-between">
							<span className="text-muted-foreground">
								<Trans>Hardware Recovery (ESP32)</Trans>
							</span>
							<span className="font-medium">{hasEsp ? <Trans>ONLINE</Trans> : <Trans>NOT INSTALLED</Trans>}</span>
						</div>
						{hasEsp && (
							<>
								<div className="flex justify-between pl-4 text-xs text-muted-foreground">
									<span>
										<Trans>Recovery Module</Trans>
									</span>
									<span>{espModule?.name || "ESP32 Module"}</span>
								</div>
								<div className="flex justify-between pl-4 text-xs text-muted-foreground">
									<span>
										<Trans>ESP IP Address</Trans>
									</span>
									<span>{espModule?.ip_address || "N/A"}</span>
								</div>
								<div className="flex justify-between pl-4 text-xs text-muted-foreground">
									<span>
										<Trans>Relay Channel</Trans>
									</span>
									<span className="font-mono">{channel.channel_number || "N/A"}</span>
								</div>
								<div className="flex justify-between items-center pl-4 text-xs text-muted-foreground">
									<span>
										<Trans>Autonomous Hardware Recovery</Trans>
									</span>
									<Switch
										checked={!channel.hardware_recovery_disabled}
										disabled={savingField === "hardware_recovery_disabled"}
										onCheckedChange={(checked) => updateChannelField("hardware_recovery_disabled", !checked)}
									/>
								</div>
								<div className="pt-2 pl-4 flex gap-2 flex-wrap">
									<Button size="sm" variant="outline" onClick={triggerManualRelay} disabled={isTriggering}>
										{isTriggering ? <LoaderCircle className="h-3 w-3 animate-spin mr-1.5" /> : null}
										<Trans>Test Relay</Trans>
									</Button>
									<Button size="sm" variant="outline" onClick={triggerManualShutdown} disabled={isTriggering}>
										{isTriggering ? (
											<LoaderCircle className="h-3 w-3 animate-spin mr-1.5" />
										) : (
											<Power className="h-3 w-3 mr-1.5" />
										)}
										<Trans>Graceful Shutdown</Trans>
									</Button>
									<Button size="sm" variant="destructive" onClick={triggerManualForceRestart} disabled={isTriggering}>
										{isTriggering ? (
											<LoaderCircle className="h-3 w-3 animate-spin mr-1.5" />
										) : (
											<RefreshCw className="h-3 w-3 mr-1.5" />
										)}
										<Trans>Force Restart</Trans>
									</Button>
								</div>
							</>
						)}
					</div>
				</CardContent>
			</Card>

			<Card>
				<CardHeader className="pb-3">
					<CardTitle className="text-md font-semibold">
						<Trans>Recent Recovery Events</Trans>
					</CardTitle>
				</CardHeader>
				<CardContent>
					{eventsLoading ? (
						<div className="flex items-center justify-center h-48 text-muted-foreground text-sm">
							<Trans>Loading events...</Trans>
						</div>
					) : (
						<>
							<div className="grid grid-cols-3 gap-2 text-center text-xs pb-3 border-b mb-3">
								<div>
									<div className="text-muted-foreground">
										<Trans>Interventions</Trans>
									</div>
									<div className="font-semibold text-sm mt-0.5">{interventions}</div>
								</div>
								<div>
									<div className="text-muted-foreground">
										<Trans>Successes</Trans>
									</div>
									<div className="font-semibold text-sm text-green-500 mt-0.5">{successes}</div>
								</div>
								<div>
									<div className="text-muted-foreground">
										<Trans>Success Rate</Trans>
									</div>
									<div className="font-semibold text-sm mt-0.5">{successRate}%</div>
								</div>
							</div>

							{stats && (
								<div className="grid grid-cols-2 gap-2 text-center text-xs pb-3 border-b mb-3">
									<div>
										<div className="text-muted-foreground">
											<Trans>Recovery Count</Trans>
										</div>
										<div className="font-semibold text-sm mt-0.5">{stats.recovery_count}</div>
									</div>
									<div>
										<div className="text-muted-foreground">
											<Trans>Last Recovery</Trans>
										</div>
										<div className="font-semibold text-sm mt-0.5">
											{stats.last_recovery ? new Date(stats.last_recovery).toLocaleString() : <Trans>Never</Trans>}
										</div>
									</div>
								</div>
							)}

							{events.length === 0 ? (
								<div className="flex items-center justify-center h-32 text-muted-foreground text-xs italic">
									<Trans>No recent recovery events</Trans>
								</div>
							) : (
								<div className="relative pl-6 border-l space-y-4 max-h-[160px] overflow-y-auto mt-2">
									{events.map((ev, idx) => {
										let dotColor = "bg-muted-foreground"
										let textColor = "text-foreground"
										if (ev.event.includes("SUCCESS") || ev.event.includes("RECOVERED")) {
											dotColor = "bg-green-500"
											textColor = "text-green-600 dark:text-green-400"
										} else if (
											ev.event.includes("FAILED") ||
											ev.event.includes("FAILURE") ||
											ev.event.includes("ERROR")
										) {
											dotColor = "bg-red-500"
											textColor = "text-red-600 dark:text-red-400"
										} else if (ev.event.includes("SENT") || ev.event.includes("STARTED")) {
											dotColor = "bg-blue-500"
											textColor = "text-blue-600 dark:text-blue-400"
										} else if (ev.event.includes("CONFIRMED")) {
											dotColor = "bg-yellow-500"
											textColor = "text-yellow-600 dark:text-yellow-400"
										}
										return (
											<div key={ev.id || idx} className="relative group">
												<span
													className={`absolute -left-[30px] top-1.5 flex h-2 w-2 rounded-full ring-4 ring-background ${dotColor}`}
												/>
												<div className="flex flex-col">
													<span className={`text-xs font-semibold ${textColor}`}>{ev.event}</span>
													<span className="text-[10px] text-muted-foreground">
														{new Date(ev.timestamp).toLocaleString()}
													</span>
													{ev.metadata && (
														<pre className="text-[10px] bg-muted/40 p-1.5 rounded mt-1 overflow-x-auto max-w-full font-mono text-muted-foreground leading-normal">
															{typeof ev.metadata === "string" ? ev.metadata : JSON.stringify(ev.metadata, null, 2)}
														</pre>
													)}
												</div>
											</div>
										)
									})}
								</div>
							)}
						</>
					)}
				</CardContent>
			</Card>
		</div>
	)
}
