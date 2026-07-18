import { t } from "@lingui/core/macro"
import { Trans } from "@lingui/react/macro"
import { redirectPage } from "@nanostores/router"
import { ExternalLinkIcon, LoaderCircleIcon, ShieldCheckIcon, ShieldAlertIcon } from "lucide-react"
import { useEffect, useState } from "react"
import { $router } from "@/components/router"
import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card"
import { Separator } from "@/components/ui/separator"
import { toast } from "@/components/ui/use-toast"
import { Input } from "@/components/ui/input"
import { Label } from "@/components/ui/label"
import { Switch } from "@/components/ui/switch"
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from "@/components/ui/select"
import {
	Dialog,
	DialogContent,
	DialogDescription,
	DialogFooter,
	DialogHeader,
	DialogTitle,
	DialogTrigger,
} from "@/components/ui/dialog"
import { isAdmin, pb } from "@/lib/api"
import { timeAgo } from "@/lib/utils"

interface RecoveryModule {
	id: string
	name: string
	mac_address: string
	ip_address?: string
	max_channels: number
	ping_interval_seconds?: number
	firmware_version: string
	status: string
	online?: boolean
	last_ping?: string
	config_revision: number
	config_hash?: string
	reported_config_revision?: number
	reported_config_hash?: string
	last_config_source?: string
	pending_esp_change?: boolean
	sync_status?: string
	health_score?: number
	health_reasons?: string[]
	gateway_ip?: string
	gateway_name?: string
	gateway_online?: boolean
	temperature?: number
	temperature_monitoring_disabled?: boolean
	temp_threshold_warning?: number
	temp_threshold_critical?: number
	buzzer_disabled?: boolean
	buzzer_muted?: boolean
	updated: string
}

interface RecoveryChannel {
	id: string
	module: string
	channel_number: number
	system: string
	host_ip: string
	failure_threshold: number
	boot_grace_seconds: number
	maintenance: boolean
	wol_enabled: boolean
	auto_wol: boolean
	mac_address?: string
	broadcast_address?: string
	wol_port?: number
	hardware_recovery_disabled: boolean
	expand?: {
		system?: {
			name: string
		}
	}
}

interface SystemItem {
	id: string
	name: string
	host: string
}

// RELAY_GPIO_PINS mirrors RELAY_PINS[] in
// firmware/esp32_recovery/esp32_recovery.ino (relay channel N drives
// RELAY_PINS[N-1]). Shown per channel so the wiring is visible in the UI.
// Keep this in sync with the firmware if the pin assignment ever changes.
const RELAY_GPIO_PINS = [18, 19, 25, 26, 27, 32]

// syncStatusMeta maps each config-sync state to a Badge variant and label,
// keeping the mapping in one place instead of repeating it inline.
const syncStatusMeta: Record<string, { variant: "success" | "warning" | "destructive" | "secondary"; label: string }> = {
	SYNCED: { variant: "success", label: "SYNCED" },
	SYNC_PENDING: { variant: "warning", label: "SYNC PENDING" },
	OFFLINE_PENDING: { variant: "warning", label: "OFFLINE PENDING" },
	CONFLICT: { variant: "destructive", label: "CONFLICT" },
	SYNC_ERROR: { variant: "destructive", label: "SYNC ERROR" },
}

function healthScoreColor(score: number): string {
	if (score >= 90) return "text-green-500"
	if (score >= 75) return "text-yellow-500"
	if (score >= 50) return "text-orange-500"
	return "text-red-500"
}

export default function RecoveryModulesSettings() {
	const [modules, setModules] = useState<RecoveryModule[]>([])
	const [channels, setChannels] = useState<RecoveryChannel[]>([])
	const [isLoading, setIsLoading] = useState(true)
	const [isApproving, setIsApproving] = useState<Record<string, boolean>>({})

	// Conflict resolution dialog state
	const [conflictDialogOpen, setConflictDialogOpen] = useState(false)
	const [conflictModuleId, setConflictModuleId] = useState("")
	const [conflictData, setConflictData] = useState<{
		esp_change?: { module?: Record<string, unknown>; channels?: Record<string, unknown>[] }
		desired?: { config_revision: number; config_hash: string }
	} | null>(null)
	const [isResolvingConflict, setIsResolvingConflict] = useState(false)

	const [dialogOpen, setDialogOpen] = useState(false)
	const [newName, setNewName] = useState("")
	const [newMac, setNewMac] = useState("")
	const [newIp, setNewIp] = useState("")
	const [newMaxChannels, setNewMaxChannels] = useState("4")
	const [newFirmware, setNewFirmware] = useState("1.0.0")

	// Systems list and channel mapping dialog states
	const [systemsList, setSystemsList] = useState<SystemItem[]>([])
	const [mappingDialogOpen, setMappingDialogOpen] = useState(false)
	const [selectedModuleId, setSelectedModuleId] = useState("")
	const [selectedChannelNum, setSelectedChannelNum] = useState<number | null>(null)
	const [existingMappingId, setExistingMappingId] = useState<string | null>(null)

	// Form fields for channel configuration
	const [mapSystemId, setMapSystemId] = useState("")
	const [mapHostIp, setMapHostIp] = useState("")
	const [mapFailureThreshold, setMapFailureThreshold] = useState("3")
	const [mapBootGraceSeconds, setMapBootGraceSeconds] = useState("60")
	const [mapMaintenance, setMapMaintenance] = useState(false)
	const [mapWolEnabled, setMapWolEnabled] = useState(false)
	const [mapAutoWol, setMapAutoWol] = useState(false)
	const [mapMacAddress, setMapMacAddress] = useState("")
	const [mapBroadcastAddress, setMapBroadcastAddress] = useState("255.255.255.255")
	const [mapWolPort, setMapWolPort] = useState("9")
	const [mapHardwareRecoveryDisabled, setMapHardwareRecoveryDisabled] = useState(false)

	async function approveModule(id: string) {
		setIsApproving((prev) => ({ ...prev, [id]: true }))
		try {
			await pb.collection("recovery_modules").update(id, {
				status: "online",
			})
			toast({
				title: t`Success`,
				description: t`Recovery module has been approved.`,
			})
			fetchData()
		} catch (error) {
			toast({
				title: t`Error`,
				description: (error as Error).message,
				variant: "destructive",
			})
		} finally {
			setIsApproving((prev) => ({ ...prev, [id]: false }))
		}
	}

	// ignoreModule/rejectModule mirror approveModule's direct-collection-update
	// pattern. Ignored/rejected modules are never treated as approved by
	// handleRecoveryPing, so a rejected ESP that keeps pinging in stays
	// unapproved-equivalent rather than being silently re-approved.
	async function ignoreModule(id: string) {
		try {
			await pb.collection("recovery_modules").update(id, { status: "ignored" })
			toast({ title: t`Success`, description: t`Recovery module ignored.` })
			fetchData()
		} catch (error) {
			toast({ title: t`Error`, description: (error as Error).message, variant: "destructive" })
		}
	}

	async function rejectModule(id: string) {
		try {
			await pb.collection("recovery_modules").update(id, { status: "rejected" })
			toast({ title: t`Success`, description: t`Recovery module rejected.` })
			fetchData()
		} catch (error) {
			toast({ title: t`Error`, description: (error as Error).message, variant: "destructive" })
		}
	}

	async function disableModule(id: string) {
		try {
			await pb.collection("recovery_modules").update(id, { status: "disabled" })
			toast({ title: t`Success`, description: t`Recovery module disabled.` })
			fetchData()
		} catch (error) {
			toast({ title: t`Error`, description: (error as Error).message, variant: "destructive" })
		}
	}

	async function reenableModule(id: string) {
		try {
			await pb.collection("recovery_modules").update(id, { status: "online" })
			toast({ title: t`Success`, description: t`Recovery module re-enabled.` })
			fetchData()
		} catch (error) {
			toast({ title: t`Error`, description: (error as Error).message, variant: "destructive" })
		}
	}

	// removeModule deletes the module record outright. Per the spec, channel
	// mappings should be cleared first so historical recovery_events (which
	// only reference the module/channel by ID, not a hard foreign key
	// requirement) remain queryable without a dangling reference assumption.
	async function removeModule(id: string, name: string) {
		if (!confirm(t`Remove "${name}" permanently? This cannot be undone. The physical ESP32 will keep running its own local watchdog independently.`)) return
		try {
			const moduleChannels = channels.filter((ch) => ch.module === id)
			for (const ch of moduleChannels) {
				await pb.collection("recovery_channels").delete(ch.id)
			}
			await pb.collection("recovery_modules").delete(id)
			toast({ title: t`Success`, description: t`Recovery module removed.` })
			fetchData()
		} catch (error) {
			toast({ title: t`Error`, description: (error as Error).message, variant: "destructive" })
		}
	}

	// updateModuleField is the shared confirmed-update path for the simple
	// module-level settings below (gateway, temperature, buzzer) - same
	// pattern as updatePingInterval, generalized to any field. Every edit
	// also bumps config_revision so the ESP picks up the change on its next
	// ping instead of the sync status sitting at SYNC PENDING indefinitely.
	async function updateModuleField(id: string, fields: Record<string, unknown>) {
		try {
			const current = modules.find((m) => m.id === id)
			const update = {
				...fields,
				config_revision: (current?.config_revision ?? 0) + 1,
				last_config_source: "BESZEL_UI",
			}
			await pb.collection("recovery_modules").update(id, update)
			setModules((prev) => prev.map((m) => (m.id === id ? { ...m, ...update } : m)))
		} catch (error) {
			toast({ title: t`Error`, description: (error as Error).message, variant: "destructive" })
		}
	}

	// bumpModuleRevision marks a module's config as changed after a channel
	// mapping write, so the ESP re-syncs the channel table on its next ping.
	// Non-fatal on failure - the caller's own toast already reported
	// success/failure of the primary channel action.
	async function bumpModuleRevision(moduleId: string) {
		const current = modules.find((m) => m.id === moduleId)
		if (!current) return
		try {
			await pb.collection("recovery_modules").update(moduleId, {
				config_revision: (current.config_revision ?? 0) + 1,
				last_config_source: "BESZEL_UI",
			})
		} catch {
			// non-fatal
		}
	}

	async function openConflictDialog(id: string) {
		setConflictModuleId(id)
		setConflictDialogOpen(true)
		setConflictData(null)
		try {
			const res = await pb.send(`/api/beszel/recovery/module/conflict`, { query: { id } })
			setConflictData(res)
		} catch (error) {
			toast({ title: t`Error`, description: (error as Error).message, variant: "destructive" })
		}
	}

	async function resolveConflict(useEsp: boolean) {
		setIsResolvingConflict(true)
		try {
			await pb.send("/api/beszel/recovery/module/conflict", {
				method: "POST",
				body: { module_id: conflictModuleId, use_esp: useEsp },
			})
			toast({
				title: t`Success`,
				description: useEsp ? t`Applied the ESP's reported configuration.` : t`Kept Beszel's configuration.`,
			})
			setConflictDialogOpen(false)
			fetchData()
		} catch (error) {
			toast({ title: t`Error`, description: (error as Error).message, variant: "destructive" })
		} finally {
			setIsResolvingConflict(false)
		}
	}

	async function handleAddModule(e: React.FormEvent) {
		e.preventDefault()
		if (!newName || !newMac) {
			toast({
				title: t`Error`,
				description: t`Name and MAC address are required.`,
				variant: "destructive",
			})
			return
		}
		try {
			const mac = newMac.trim().toLowerCase()
			await pb.collection("recovery_modules").create({
				name: newName.trim(),
				mac_address: mac,
				ip_address: newIp.trim() || undefined,
				max_channels: Number(newMaxChannels),
				firmware_version: newFirmware.trim() || "1.0.0",
				status: "online",
				config_revision: 1,
			})
			toast({
				title: t`Success`,
				description: t`Recovery module added successfully.`,
			})
			setDialogOpen(false)
			setNewName("")
			setNewMac("")
			setNewIp("")
			setNewMaxChannels("4")
			setNewFirmware("1.0.0")
			fetchData()
		} catch (error) {
			toast({
				title: t`Error`,
				description: (error as Error).message,
				variant: "destructive",
			})
		}
	}

	function openMappingDialog(moduleId: string, channelNum: number, existing?: RecoveryChannel) {
		setSelectedModuleId(moduleId)
		setSelectedChannelNum(channelNum)
		if (existing) {
			setExistingMappingId(existing.id)
			setMapSystemId(existing.system)
			setMapHostIp(existing.host_ip || "")
			setMapFailureThreshold(String(existing.failure_threshold || 3))
			setMapBootGraceSeconds(String(existing.boot_grace_seconds || 60))
			setMapMaintenance(existing.maintenance || false)
			setMapWolEnabled(existing.wol_enabled || false)
			setMapAutoWol(existing.auto_wol || false)
			setMapMacAddress(existing.mac_address || "")
			setMapBroadcastAddress(existing.broadcast_address || "255.255.255.255")
			setMapWolPort(String(existing.wol_port || 9))
			setMapHardwareRecoveryDisabled(existing.hardware_recovery_disabled || false)
		} else {
			setExistingMappingId(null)
			setMapSystemId("")
			setMapHostIp("")
			setMapFailureThreshold("3")
			setMapBootGraceSeconds("60")
			setMapMaintenance(false)
			setMapWolEnabled(false)
			setMapAutoWol(false)
			setMapMacAddress("")
			setMapBroadcastAddress("255.255.255.255")
			setMapWolPort("9")
			setMapHardwareRecoveryDisabled(false)
		}
		setMappingDialogOpen(true)
	}

	async function handleSaveMapping(e: React.FormEvent) {
		e.preventDefault()
		if (!mapSystemId) {
			toast({
				title: t`Error`,
				description: t`System is required.`,
				variant: "destructive",
			})
			return
		}

		let hostIp = mapHostIp.trim()
		if (!hostIp) {
			const sys = systemsList.find((s) => s.id === mapSystemId)
			if (sys) {
				hostIp = sys.host
			}
		}

		const data = {
			module: selectedModuleId,
			channel_number: selectedChannelNum,
			system: mapSystemId,
			host_ip: hostIp,
			failure_threshold: Number(mapFailureThreshold),
			boot_grace_seconds: Number(mapBootGraceSeconds),
			maintenance: mapMaintenance,
			wol_enabled: mapWolEnabled,
			auto_wol: mapAutoWol,
			mac_address: mapMacAddress.trim() || undefined,
			broadcast_address: mapBroadcastAddress.trim() || "255.255.255.255",
			wol_port: Number(mapWolPort),
			hardware_recovery_disabled: mapHardwareRecoveryDisabled,
		}

		try {
			if (existingMappingId) {
				await pb.collection("recovery_channels").update(existingMappingId, data)
				toast({
					title: t`Success`,
					description: t`Channel mapping updated.`,
				})
			} else {
				await pb.collection("recovery_channels").create(data)
				toast({
					title: t`Success`,
					description: t`Channel mapped successfully.`,
				})
			}
			await bumpModuleRevision(selectedModuleId)
			setMappingDialogOpen(false)
			fetchData()
		} catch (error) {
			toast({
				title: t`Error`,
				description: (error as Error).message,
				variant: "destructive",
			})
		}
	}

	async function handleUnmapChannel() {
		if (!existingMappingId) return
		if (!confirm(t`Are you sure you want to unmap this channel?`)) return
		try {
			await pb.collection("recovery_channels").delete(existingMappingId)
			await bumpModuleRevision(selectedModuleId)
			toast({
				title: t`Success`,
				description: t`Channel mapping removed.`,
			})
			setMappingDialogOpen(false)
			fetchData()
		} catch (error) {
			toast({
				title: t`Error`,
				description: (error as Error).message,
				variant: "destructive",
			})
		}
	}

	async function updatePingInterval(id: string, value: number) {
		try {
			const current = modules.find((m) => m.id === id)
			const update = {
				ping_interval_seconds: value,
				config_revision: (current?.config_revision ?? 0) + 1,
				last_config_source: "BESZEL_UI",
			}
			await pb.collection("recovery_modules").update(id, update)
			toast({ title: t`Success`, description: t`Ping interval updated.` })
			setModules((prev) => prev.map((m) => (m.id === id ? { ...m, ...update } : m)))
		} catch (error) {
			toast({ title: t`Error`, description: (error as Error).message, variant: "destructive" })
		}
	}

	if (!isAdmin()) {
		redirectPage($router, "settings", { name: "general" })
	}

	useEffect(() => {
		fetchData()
	}, [])

	async function fetchData() {
		try {
			setIsLoading(true)
			const modulesRes = await pb.send<RecoveryModule[]>("/api/beszel/recovery/modules", {})
			const channelsRes = await pb.collection("recovery_channels").getFullList<RecoveryChannel>({
				expand: "system",
			})
			const systemsRes = await pb.collection("systems").getFullList<SystemItem>()
			setModules(modulesRes || [])
			setChannels(channelsRes || [])
			setSystemsList(systemsRes || [])
		} catch (error: unknown) {
			toast({
				title: t`Error`,
				description: (error as Error).message,
				variant: "destructive",
			})
		} finally {
			setIsLoading(false)
		}
	}

	return (
		<div>
			<div className="flex justify-between items-center mb-4">
				<div>
					<h3 className="text-xl font-medium mb-1">
						<Trans>Hardware Recovery Modules</Trans>
					</h3>
					<p className="text-sm text-muted-foreground leading-relaxed">
						<Trans>Manage ESP32 hardware watchdog and physical relay controllers.</Trans>
					</p>
				</div>
				<div className="flex items-center gap-2">
					<Dialog open={dialogOpen} onOpenChange={setDialogOpen}>
						<DialogTrigger asChild>
							<Button size="sm" variant="outline">
								<Trans>Add Device</Trans>
							</Button>
						</DialogTrigger>
						<DialogContent>
							<DialogHeader>
								<DialogTitle>
									<Trans>Add Recovery Module</Trans>
								</DialogTitle>
								<DialogDescription>
									<Trans>Manually register an ESP32 hardware recovery module.</Trans>
								</DialogDescription>
							</DialogHeader>
							<form onSubmit={handleAddModule} className="space-y-4">
								<div className="grid gap-2">
									<Label htmlFor="name">
										<Trans>Name</Trans>
									</Label>
									<Input
										id="name"
										value={newName}
										onChange={(e) => setNewName(e.target.value)}
										placeholder={t`e.g. Rack A Watchdog`}
										required
									/>
								</div>
								<div className="grid gap-2">
									<Label htmlFor="mac">
										<Trans>MAC Address</Trans>
									</Label>
									<Input
										id="mac"
										value={newMac}
										onChange={(e) => setNewMac(e.target.value)}
										placeholder="e.g. aa:bb:cc:dd:ee:ff"
										required
									/>
								</div>
								<div className="grid gap-2">
									<Label htmlFor="ip">
										<Trans>IP Address (Optional)</Trans>
									</Label>
									<Input
										id="ip"
										value={newIp}
										onChange={(e) => setNewIp(e.target.value)}
										placeholder="e.g. 192.168.1.150"
									/>
								</div>
								<div className="grid sm:grid-cols-2 gap-4">
									<div className="grid gap-2">
										<Label htmlFor="max_channels">
											<Trans>Max Channels</Trans>
										</Label>
										<Input
											id="max_channels"
											type="number"
											min={1}
											value={newMaxChannels}
											onChange={(e) => setNewMaxChannels(e.target.value)}
											required
										/>
									</div>
									<div className="grid gap-2">
										<Label htmlFor="firmware">
											<Trans>Firmware Version</Trans>
										</Label>
										<Input
											id="firmware"
											value={newFirmware}
											onChange={(e) => setNewFirmware(e.target.value)}
											placeholder="1.0.0"
										/>
									</div>
								</div>
								<DialogFooter className="pt-2">
									<Button type="button" variant="ghost" onClick={() => setDialogOpen(false)}>
										<Trans>Cancel</Trans>
									</Button>
									<Button type="submit">
										<Trans>Add Device</Trans>
									</Button>
								</DialogFooter>
							</form>
						</DialogContent>
					</Dialog>
					<Button size="sm" onClick={fetchData} disabled={isLoading}>
						{isLoading ? <LoaderCircleIcon className="h-4 w-4 animate-spin mr-2" /> : null}
						<Trans>Refresh</Trans>
					</Button>
				</div>
			</div>
			<Separator className="my-4" />

			{isLoading ? (
				<div className="flex justify-center items-center h-48 text-muted-foreground text-sm">
					<LoaderCircleIcon className="h-8 w-8 animate-spin mr-3" />
					<Trans>Loading modules and channels...</Trans>
				</div>
			) : modules.length === 0 ? (
				<div className="flex flex-col items-center justify-center border-2 border-dashed rounded-lg p-12 text-center text-muted-foreground">
					<ShieldAlertIcon className="h-10 w-10 mb-3 text-muted-foreground/60" />
					<h4 className="font-semibold text-md mb-1">
						<Trans>No Recovery Modules Registered</Trans>
					</h4>
					<p className="text-sm max-w-sm mb-4">
						<Trans>Once you plug an ESP32 Recovery Module into your network, approval requests will appear here.</Trans>
					</p>
				</div>
			) : (
				<div className="space-y-6">
					{modules.map((mod) => {
						const moduleChannels = channels.filter((ch) => ch.module === mod.id)
						const isUnapproved = mod.status === "unapproved"
						const isDisabled = mod.status === "disabled"
						const isOnline = mod.online ?? false
						const temp = mod.temperature ?? 0
						const tempWarn = mod.temp_threshold_warning || 50
						const tempCrit = mod.temp_threshold_critical || 60
						let tempColor = "text-green-500 font-semibold"
						if (temp > tempCrit) {
							tempColor = "text-red-500 font-bold animate-pulse"
						} else if (temp > tempWarn) {
							tempColor = "text-yellow-500 font-bold"
						}
						return (
							<Card key={mod.id}>
								<CardHeader className="flex flex-row items-center justify-between space-y-0 pb-3">
									<div>
										<CardTitle className="text-lg font-semibold flex items-center gap-2">
											{mod.name}
											<Badge variant={isUnapproved ? "warning" : isDisabled ? "secondary" : isOnline ? "success" : "secondary"}>
												{isUnapproved ? (
													<Trans>WAITING APPROVAL</Trans>
												) : isDisabled ? (
													<Trans>DISABLED</Trans>
												) : isOnline ? (
													<Trans>ONLINE</Trans>
												) : (
													<Trans>OFFLINE</Trans>
												)}
											</Badge>
										</CardTitle>
										<CardDescription className="font-mono text-xs mt-1">
											MAC: {mod.mac_address} | Firmware: {mod.firmware_version}
											{mod.temperature !== undefined && mod.temperature > 0 && (
												<>
													{" | "}
													<Trans>Temp</Trans>:{" "}
													<span className={tempColor}>{mod.temperature.toFixed(1)}°C</span>
												</>
											)}
										</CardDescription>
									</div>
									{isUnapproved ? (
										<div className="flex flex-wrap items-center gap-2 justify-end">
											<Button size="sm" variant="ghost" onClick={() => rejectModule(mod.id)}>
												<Trans>Reject</Trans>
											</Button>
											<Button size="sm" variant="outline" onClick={() => ignoreModule(mod.id)}>
												<Trans>Ignore</Trans>
											</Button>
											<Button size="sm" onClick={() => approveModule(mod.id)} disabled={isApproving[mod.id]}>
												{isApproving[mod.id] ? <LoaderCircleIcon className="h-4 w-4 animate-spin mr-2" /> : null}
												<Trans>Approve Module</Trans>
											</Button>
										</div>
									) : (
										<div className="flex flex-col items-end gap-1.5 max-w-full">
											<div className="flex flex-wrap items-center gap-2 justify-end">
												{mod.ip_address && (
													<Button variant="outline" size="sm" asChild>
														<a href={`http://${mod.ip_address}`} target="_blank" rel="noopener noreferrer">
															<Trans>Open Local ESP Portal</Trans>
															<ExternalLinkIcon className="h-3 w-3 ml-1.5" />
														</a>
													</Button>
												)}
												{isDisabled ? (
													<Button size="sm" variant="outline" onClick={() => reenableModule(mod.id)}>
														<Trans>Re-enable</Trans>
													</Button>
												) : (
													<Button size="sm" variant="outline" onClick={() => disableModule(mod.id)}>
														<Trans>Disable</Trans>
													</Button>
												)}
												<Button size="sm" variant="destructive" onClick={() => removeModule(mod.id, mod.name)}>
													<Trans>Remove</Trans>
												</Button>
											</div>
											{mod.ip_address && (
												<span className="text-[10px] text-muted-foreground text-right max-w-[240px] leading-tight">
													<Trans>Address is LAN-local and may be stale if the module is offline.</Trans>
												</span>
											)}
										</div>
									)}
								</CardHeader>
								<CardContent className="space-y-4">
									<div className="grid grid-cols-1 sm:grid-cols-2 xl:grid-cols-4 gap-x-8 gap-y-5 text-sm border-t pt-4 items-start">
										<div>
											<span className="text-muted-foreground">
												<Trans>Config Synchronization</Trans>
											</span>
											<div className="font-semibold flex items-center gap-1.5 mt-0.5">
												<ShieldCheckIcon
													className={`h-4 w-4 ${
														mod.sync_status === "SYNCED"
															? "text-green-500"
															: mod.sync_status === "CONFLICT" || mod.sync_status === "SYNC_ERROR"
																? "text-red-500"
																: "text-muted-foreground"
													}`}
												/>
												{isUnapproved ? (
													<Trans>UNAPPROVED</Trans>
												) : (
													<Badge variant={syncStatusMeta[mod.sync_status ?? ""]?.variant ?? (isOnline ? "success" : "secondary")}>
														{syncStatusMeta[mod.sync_status ?? ""]?.label ?? (isOnline ? "SYNCED" : "OFFLINE PENDING")}
													</Badge>
												)}
											</div>
											{!isUnapproved && mod.sync_status === "CONFLICT" && (
												<Button
													size="sm"
													variant="destructive"
													className="mt-1.5 h-6 px-2 text-[11px]"
													onClick={() => openConflictDialog(mod.id)}
												>
													<Trans>Resolve Conflict</Trans>
												</Button>
											)}
										</div>
										<div>
											<span className="text-muted-foreground">
												<Trans>Config Revision</Trans>
											</span>
											<div className="font-semibold mt-0.5">{mod.config_revision}</div>
										</div>
										<div>
											<span className="text-muted-foreground">
												<Trans>Ping Interval (s)</Trans>
											</span>
											<div className="mt-0.5">
												<Input
													type="number"
													className="h-7 w-20 px-2 py-1 text-xs"
													defaultValue={mod.ping_interval_seconds || 30}
													onBlur={(e) => {
														const val = parseInt(e.target.value, 10)
														if (!Number.isNaN(val) && val >= 5) updatePingInterval(mod.id, val)
													}}
												/>
											</div>
										</div>
										<div>
											<span className="text-muted-foreground">
												<Trans>Health Score</Trans>
											</span>
											<div
												className={`font-semibold mt-0.5 ${healthScoreColor(mod.health_score ?? 0)}`}
												title={mod.health_reasons?.join(", ")}
											>
												{mod.health_score !== undefined ? `${mod.health_score}%` : "N/A"}
											</div>
										</div>
										<div>
											<span className="text-muted-foreground">
												<Trans>Last Heartbeat</Trans>
											</span>
											<div className="font-semibold mt-0.5">{timeAgo(mod.last_ping)}</div>
										</div>
										<div>
											<span className="text-muted-foreground">
												<Trans>Gateway</Trans>
											</span>
											<div className="mt-0.5 space-y-1">
												<div className="flex items-center gap-1">
													<ShieldCheckIcon
														className={`h-3.5 w-3.5 shrink-0 ${mod.gateway_online ? "text-green-500" : "text-muted-foreground"}`}
													/>
													<Input
														className="h-7 px-2 py-1 text-xs"
														defaultValue={mod.gateway_name || ""}
														placeholder={t`Gateway name`}
														onBlur={(e) => {
															const val = e.target.value.trim()
															if (val !== (mod.gateway_name || "")) updateModuleField(mod.id, { gateway_name: val })
														}}
													/>
												</div>
												<Input
													className="h-7 px-2 py-1 text-xs font-mono"
													defaultValue={mod.gateway_ip || ""}
													placeholder={t`Gateway IP`}
													onBlur={(e) => {
														const val = e.target.value.trim()
														if (val !== (mod.gateway_ip || "")) updateModuleField(mod.id, { gateway_ip: val })
													}}
												/>
											</div>
										</div>
										<div>
											<span className="text-muted-foreground">
												<Trans>Temperature Monitoring</Trans>
											</span>
											<div className="mt-1 flex items-center gap-2">
												<Switch
													checked={!mod.temperature_monitoring_disabled}
													onCheckedChange={(checked) =>
														updateModuleField(mod.id, { temperature_monitoring_disabled: !checked })
													}
												/>
												<span className="text-xs text-muted-foreground">
													{mod.temperature_monitoring_disabled ? <Trans>Off</Trans> : <Trans>On</Trans>}
												</span>
											</div>
											{!mod.temperature_monitoring_disabled && (
												<div className="flex items-center gap-1 mt-1.5 text-xs text-muted-foreground flex-wrap">
													<Trans>Warn</Trans>
													<Input
														type="number"
														className="h-6 w-14 px-1 py-0.5 text-xs"
														defaultValue={tempWarn}
														onBlur={(e) => {
															const val = parseInt(e.target.value, 10)
															if (!Number.isNaN(val)) updateModuleField(mod.id, { temp_threshold_warning: val })
														}}
													/>
													°C /
													<Trans>Crit</Trans>
													<Input
														type="number"
														className="h-6 w-14 px-1 py-0.5 text-xs"
														defaultValue={tempCrit}
														onBlur={(e) => {
															const val = parseInt(e.target.value, 10)
															if (!Number.isNaN(val)) updateModuleField(mod.id, { temp_threshold_critical: val })
														}}
													/>
													°C
												</div>
											)}
										</div>
										<div>
											<span className="text-muted-foreground">
												<Trans>Buzzer</Trans>
											</span>
											<div className="mt-1 flex flex-wrap items-center gap-3">
												<div className="flex items-center gap-1.5">
													<Switch
														checked={!mod.buzzer_disabled}
														onCheckedChange={(checked) => updateModuleField(mod.id, { buzzer_disabled: !checked })}
													/>
													<span className="text-xs text-muted-foreground whitespace-nowrap">
														<Trans>Enabled</Trans>
													</span>
												</div>
												<div className="flex items-center gap-1.5">
													<Switch
														checked={mod.buzzer_muted ?? false}
														disabled={mod.buzzer_disabled}
														onCheckedChange={(checked) => updateModuleField(mod.id, { buzzer_muted: checked })}
													/>
													<span className="text-xs text-muted-foreground whitespace-nowrap">
														<Trans>Mute</Trans>
													</span>
												</div>
											</div>
										</div>
									</div>

									<Separator />

									<div>
										<h4 className="text-sm font-semibold mb-3">
											<Trans>Physical watchdogs & channels</Trans>
										</h4>
										<div className="space-y-2.5">
											{Array.from({ length: mod.max_channels }).map((_, idx) => {
												const chanNum = idx + 1
												const mapping = moduleChannels.find((ch) => ch.channel_number === chanNum)
												return (
													<div
														key={chanNum}
														className="flex justify-between items-center p-3 rounded-lg border text-sm bg-muted/40"
													>
														<div className="space-y-1">
															<div className="font-semibold text-primary flex items-center gap-2">
																Channel {chanNum}
																<span className="text-[10px] font-normal text-muted-foreground">
																	GPIO {RELAY_GPIO_PINS[chanNum - 1] ?? "?"}
																</span>
															</div>
															{mapping ? (
																<div className="text-xs text-muted-foreground">
																	Target:{" "}
																	<span className="font-medium text-foreground">
																		{mapping.expand?.system?.name || mapping.system}
																	</span>
																</div>
															) : (
																<div className="text-xs text-muted-foreground">
																	<Trans>Unmapped / Available</Trans>
																</div>
															)}
														</div>
														<div className="flex items-center gap-2">
															{mapping ? (
																mapping.maintenance ? (
																	<Badge variant="warning">
																		<Trans>MAINTENANCE</Trans>
																	</Badge>
																) : (
																	<Badge variant="success">
																		<Trans>PROTECTED</Trans>
																	</Badge>
																)
															) : (
																<Badge variant="secondary">
																	<Trans>UNUSED</Trans>
																</Badge>
															)}
															<Button
																size="sm"
																variant="ghost"
																className="h-7 px-2"
																onClick={() => openMappingDialog(mod.id, chanNum, mapping)}
															>
																<Trans>Configure</Trans>
															</Button>
														</div>
													</div>
												)
											})}
										</div>
									</div>
								</CardContent>
							</Card>
						)
					})}
				</div>
			)}

			<Dialog open={mappingDialogOpen} onOpenChange={setMappingDialogOpen}>
				<DialogContent className="max-w-md">
					<DialogHeader>
						<DialogTitle>
							{existingMappingId ? <Trans>Edit Channel Mapping</Trans> : <Trans>Configure Channel Mapping</Trans>}
						</DialogTitle>
						<DialogDescription>
							<Trans>Map system to channel {selectedChannelNum} of module {modules.find(m => m.id === selectedModuleId)?.name}.</Trans>
						</DialogDescription>
					</DialogHeader>
					<form onSubmit={handleSaveMapping} className="space-y-4">
						<div className="grid gap-2">
							<Label htmlFor="map_system">
								<Trans>System to protect</Trans>
							</Label>
							<Select value={mapSystemId} onValueChange={setMapSystemId}>
								<SelectTrigger id="map_system">
									<SelectValue placeholder={t`Select a system...`} />
								</SelectTrigger>
								<SelectContent>
									{systemsList
										.filter((sys) => !channels.some((ch) => ch.system === sys.id && ch.id !== existingMappingId))
										.map((sys) => (
											<SelectItem key={sys.id} value={sys.id}>
												{sys.name} ({sys.host})
											</SelectItem>
										))}
								</SelectContent>
							</Select>
						</div>

						<div className="grid gap-2">
							<Label htmlFor="map_host_ip">
								<Trans>Host IP to probe (Optional)</Trans>
							</Label>
							<Input
								id="map_host_ip"
								value={mapHostIp}
								onChange={(e) => setMapHostIp(e.target.value)}
								placeholder={t`Fallback to system host IP`}
							/>
						</div>

						<div className="grid sm:grid-cols-2 gap-2">
							<div className="grid gap-1">
								<Label htmlFor="map_threshold">
									<Trans>Threshold</Trans>
								</Label>
								<Input
									id="map_threshold"
									type="number"
									min={1}
									value={mapFailureThreshold}
									onChange={(e) => setMapFailureThreshold(e.target.value)}
									required
								/>
							</div>
							<div className="grid gap-1">
								<Label htmlFor="map_grace">
									<Trans>Grace (s)</Trans>
								</Label>
								<Input
									id="map_grace"
									type="number"
									min={5}
									value={mapBootGraceSeconds}
									onChange={(e) => setMapBootGraceSeconds(e.target.value)}
									required
								/>
							</div>
						</div>

						<div className="flex items-center justify-between border p-2 rounded-lg text-sm bg-muted/20">
							<div className="space-y-0.5">
								<Label className="text-sm font-medium">
									<Trans>Maintenance Mode</Trans>
								</Label>
								<p className="text-xs text-muted-foreground">
									<Trans>Pause watchdogs during system maintenance.</Trans>
								</p>
							</div>
							<Switch
								checked={mapMaintenance}
								onCheckedChange={setMapMaintenance}
							/>
						</div>

						<div className="border p-3 rounded-lg space-y-3 bg-muted/10">
							<div className="flex items-center justify-between">
								<div className="space-y-0.5">
									<Label className="text-sm font-medium">
										<Trans>Wake-on-LAN (WOL)</Trans>
									</Label>
									<p className="text-xs text-muted-foreground">
										<Trans>Enable Wake-on-LAN recovery packets.</Trans>
									</p>
								</div>
								<Switch
									checked={mapWolEnabled}
									onCheckedChange={setMapWolEnabled}
								/>
							</div>

							{mapWolEnabled && (
								<div className="space-y-2 pt-2 border-t">
									<div className="flex items-center justify-between">
										<Label htmlFor="map_auto_wol" className="text-xs">
											<Trans>Automatic WOL</Trans>
										</Label>
										<Switch
											id="map_auto_wol"
											checked={mapAutoWol}
											onCheckedChange={setMapAutoWol}
										/>
									</div>
									<div className="grid gap-1">
										<Label htmlFor="map_mac" className="text-xs">
											<Trans>MAC Address</Trans>
										</Label>
										<Input
											id="map_mac"
											value={mapMacAddress}
											onChange={(e) => setMapMacAddress(e.target.value)}
											placeholder="aa:bb:cc:dd:ee:ff"
										/>
									</div>
									<div className="grid sm:grid-cols-2 gap-2">
										<div className="grid gap-1">
											<Label htmlFor="map_bcast" className="text-xs">
												<Trans>Broadcast IP</Trans>
											</Label>
											<Input
												id="map_bcast"
												value={mapBroadcastAddress}
												onChange={(e) => setMapBroadcastAddress(e.target.value)}
												placeholder="255.255.255.255"
											/>
										</div>
										<div className="grid gap-1">
											<Label htmlFor="map_wol_port" className="text-xs">
												<Trans>WOL Port</Trans>
											</Label>
											<Input
												id="map_wol_port"
												type="number"
												min={1}
												value={mapWolPort}
												onChange={(e) => setMapWolPort(e.target.value)}
											/>
										</div>
									</div>
								</div>
							)}
						</div>

						<div className="flex items-center justify-between border p-2 rounded-lg text-sm bg-muted/20">
							<div className="space-y-0.5">
								<Label className="text-sm font-medium">
									<Trans>Autonomous Hardware Recovery</Trans>
								</Label>
								<p className="text-xs text-muted-foreground">
									<Trans>Allow the ESP32 to trigger the physical relay.</Trans>
								</p>
							</div>
							<Switch
								checked={!mapHardwareRecoveryDisabled}
								onCheckedChange={(checked) => setMapHardwareRecoveryDisabled(!checked)}
							/>
						</div>

						<DialogFooter className="pt-2 flex justify-between gap-2">
							{existingMappingId && (
								<Button type="button" variant="destructive" className="mr-auto" onClick={handleUnmapChannel}>
									<Trans>Unmap Channel</Trans>
								</Button>
							)}
							<div className="flex gap-2 ml-auto">
								<Button type="button" variant="ghost" onClick={() => setMappingDialogOpen(false)}>
									<Trans>Cancel</Trans>
								</Button>
								<Button type="submit">
									<Trans>Save Configuration</Trans>
								</Button>
							</div>
						</DialogFooter>
					</form>
				</DialogContent>
			</Dialog>

			<Dialog open={conflictDialogOpen} onOpenChange={setConflictDialogOpen}>
				<DialogContent className="max-w-lg">
					<DialogHeader>
						<DialogTitle>
							<Trans>Resolve Configuration Conflict</Trans>
						</DialogTitle>
						<DialogDescription>
							<Trans>
								This module reported a local settings change that was made while Beszel's desired configuration had
								already moved on. Choose which side's configuration should win.
							</Trans>
						</DialogDescription>
					</DialogHeader>
					{!conflictData ? (
						<div className="flex justify-center items-center h-24 text-muted-foreground text-sm">
							<LoaderCircleIcon className="h-5 w-5 animate-spin mr-2" />
							<Trans>Loading conflict details...</Trans>
						</div>
					) : (
						<div className="grid sm:grid-cols-2 gap-3 text-sm">
							<div className="border rounded-lg p-3 space-y-1.5 bg-muted/20">
								<div className="font-semibold flex items-center gap-1.5">
									<Trans>Beszel (Desired)</Trans>
								</div>
								<div className="text-xs text-muted-foreground">
									<Trans>Revision</Trans>: {conflictData.desired?.config_revision}
								</div>
								<div className="text-xs font-mono break-all text-muted-foreground">
									{conflictData.desired?.config_hash}
								</div>
							</div>
							<div className="border rounded-lg p-3 space-y-1.5 bg-muted/20">
								<div className="font-semibold flex items-center gap-1.5">
									<Trans>ESP (Local Change)</Trans>
								</div>
								{conflictData.esp_change?.module && (
									<pre className="text-[11px] whitespace-pre-wrap break-all text-muted-foreground">
										{JSON.stringify(conflictData.esp_change.module, null, 2)}
									</pre>
								)}
								{conflictData.esp_change?.channels && conflictData.esp_change.channels.length > 0 && (
									<pre className="text-[11px] whitespace-pre-wrap break-all text-muted-foreground">
										{JSON.stringify(conflictData.esp_change.channels, null, 2)}
									</pre>
								)}
							</div>
						</div>
					)}
					<DialogFooter className="pt-2">
						<Button
							variant="outline"
							disabled={isResolvingConflict || !conflictData}
							onClick={() => resolveConflict(false)}
						>
							{isResolvingConflict ? <LoaderCircleIcon className="h-4 w-4 animate-spin mr-2" /> : null}
							<Trans>Keep Beszel Value</Trans>
						</Button>
						<Button disabled={isResolvingConflict || !conflictData} onClick={() => resolveConflict(true)}>
							{isResolvingConflict ? <LoaderCircleIcon className="h-4 w-4 animate-spin mr-2" /> : null}
							<Trans>Use ESP Value</Trans>
						</Button>
					</DialogFooter>
				</DialogContent>
			</Dialog>
		</div>
	)
}
