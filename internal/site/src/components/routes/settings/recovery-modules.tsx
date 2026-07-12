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
import { isAdmin, pb } from "@/lib/api"

interface RecoveryModule {
	id: string
	name: string
	mac_address: string
	ip_address?: string
	max_channels: number
	firmware_version: string
	status: string
	config_revision: number
	config_hash?: string
	updated: string
}

interface RecoveryChannel {
	id: string
	module: string
	channel_number: number
	system: string
	host_ip: string
	probe_ports: number[]
	failure_threshold: number
	boot_grace_seconds: number
	maintenance: boolean
	expand?: {
		system?: {
			name: string
		}
	}
}

export default function RecoveryModulesSettings() {
	const [modules, setModules] = useState<RecoveryModule[]>([])
	const [channels, setChannels] = useState<RecoveryChannel[]>([])
	const [isLoading, setIsLoading] = useState(true)
	const [isApproving, setIsApproving] = useState<Record<string, boolean>>({})

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
			setModules(modulesRes || [])
			setChannels(channelsRes || [])
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
				<Button size="sm" onClick={fetchData} disabled={isLoading}>
					{isLoading ? <LoaderCircleIcon className="h-4 w-4 animate-spin mr-2" /> : null}
					<Trans>Refresh</Trans>
				</Button>
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
						const isOnline = mod.status === "online" || mod.status === "ONLINE"
						const temp = mod.temperature
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
											<Badge variant={isUnapproved ? "warning" : isOnline ? "success" : "secondary"}>
												{isUnapproved ? <Trans>WAITING APPROVAL</Trans> : isOnline ? <Trans>ONLINE</Trans> : <Trans>OFFLINE</Trans>}
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
										<Button
											size="sm"
											onClick={() => approveModule(mod.id)}
											disabled={isApproving[mod.id]}
										>
											{isApproving[mod.id] ? <LoaderCircleIcon className="h-4 w-4 animate-spin mr-2" /> : null}
											<Trans>Approve Module</Trans>
										</Button>
									) : (
										mod.ip_address && (
											<div className="flex flex-col items-end gap-1.5">
												<Button variant="outline" size="sm" asChild>
													<a href={`http://${mod.ip_address}`} target="_blank" rel="noopener noreferrer">
														<Trans>Open Local ESP Portal</Trans>
														<ExternalLinkIcon className="h-3 w-3 ml-1.5" />
													</a>
												</Button>
												<span className="text-[10px] text-muted-foreground text-right max-w-[200px] leading-tight">
													<Trans>Address is LAN-local and may be stale if the module is offline.</Trans>
												</span>
											</div>
										)
									)}
								</CardHeader>
								<CardContent className="space-y-4">
									<div className="grid grid-cols-3 gap-4 text-sm border-t pt-4">
										<div>
											<span className="text-muted-foreground">
												<Trans>Config Synchronization</Trans>
											</span>
											<div className="font-semibold flex items-center gap-1.5 mt-0.5">
												<ShieldCheckIcon
													className={`h-4 w-4 ${isOnline ? "text-green-500" : "text-muted-foreground"}`}
												/>
												{isUnapproved ? (
													<Trans>UNAPPROVED</Trans>
												) : isOnline ? (
													<Trans>SYNCED</Trans>
												) : (
													<Trans>OFFLINE PENDING</Trans>
												)}
											</div>
										</div>
										<div>
											<span className="text-muted-foreground">
												<Trans>Config Revision</Trans>
											</span>
											<div className="font-semibold mt-0.5">{mod.config_revision}</div>
										</div>
										<div>
											<span className="text-muted-foreground">
												<Trans>Temperature Thresholds</Trans>
											</span>
											<div className="font-semibold text-xs mt-0.5">
												<Trans>Warn</Trans>: {mod.temp_threshold_warning || 50}°C / <Trans>Crit</Trans>: {mod.temp_threshold_critical || 60}°C
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
															<div className="font-semibold text-primary">Channel {chanNum}</div>
															{mapping ? (
																<div className="text-xs text-muted-foreground">
																	Target:{" "}
																	<span className="font-medium text-foreground">
																		{mapping.expand?.system?.name || mapping.system}
																	</span>{" "}
																	| Ports: {mapping.probe_ports?.join(", ")}
																</div>
															) : (
																<div className="text-xs text-muted-foreground">
																	<Trans>Unmapped / Available</Trans>
																</div>
															)}
														</div>
														<div>
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
		</div>
	)
}
