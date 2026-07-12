import { useEffect, useState } from "react"
import { t } from "@lingui/core/macro"
import { Trans } from "@lingui/react/macro"
import { pb } from "@/lib/api"
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card"
import { Separator } from "@/components/ui/separator"
import { Button } from "@/components/ui/button"
import { toast } from "@/components/ui/use-toast"
import { ShieldCheck, ShieldAlert, AlertTriangle, LoaderCircle } from "lucide-react"

interface RecoveryInfoProps {
	systemId: string
	info: any
}

export default function RecoveryInfo({ systemId, info }: RecoveryInfoProps) {
	const [events, setEvents] = useState<any[]>([])
	const [loading, setLoading] = useState(true)
	const [isWaking, setIsWaking] = useState(false)
	const [isTriggering, setIsTriggering] = useState(false)

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
			const res = await pb.send("/api/beszel/recovery/events", {
				query: { system: systemId },
			})
			setEvents(res || [])
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
			const res = await pb.send("/api/beszel/recovery/events", {
				query: { system: systemId },
			})
			setEvents(res || [])
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

	useEffect(() => {
		let isMounted = true
		async function fetchEvents() {
			try {
				const res = await pb.send("/api/beszel/recovery/events", {
					query: { system: systemId },
				})
				if (isMounted) {
					setEvents(res || [])
				}
			} catch (e) {
				console.error(e)
			} finally {
				if (isMounted) {
					setLoading(false)
				}
			}
		}
		fetchEvents()
		return () => {
			isMounted = false
		}
	}, [systemId])

	const hasWol = info.wol_enabled
	const hasEsp = info.esp_mapped
	const hasMaint = info.maintenance
	const isEspOffline = info.esp_offline

	let healthScore = 100
	let statusLabel = <Trans>HEALTHY</Trans>
	let statusColor = "text-green-500"
	let Icon = ShieldCheck

	if (!hasEsp && !hasWol) {
		return null
	}

	if (hasMaint) {
		healthScore = 80
		statusLabel = <Trans>MAINTENANCE</Trans>
		statusColor = "text-yellow-500"
		Icon = AlertTriangle
	} else if (isEspOffline) {
		healthScore = 45
		statusLabel = <Trans>DEGRADED</Trans>
		statusColor = "text-red-500"
		Icon = ShieldAlert
	}

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
						<span className={`font-semibold ${statusColor}`}>
							{statusLabel} ({healthScore}%)
						</span>
					</div>
					<Separator />
					<div className="space-y-2 text-sm">
						<div className="flex justify-between">
							<span className="text-muted-foreground">
								<Trans>Wake-on-LAN</Trans>
							</span>
							<span className="font-medium">{hasWol ? <Trans>ENABLED</Trans> : <Trans>DISABLED</Trans>}</span>
						</div>
						{hasWol && (
							<>
								<div className="flex justify-between pl-4 text-xs text-muted-foreground">
									<span>
										<Trans>Automatic WOL</Trans>
									</span>
									<span>{info.auto_wol ? <Trans>YES</Trans> : <Trans>NO</Trans>}</span>
								</div>
								<div className="flex justify-between pl-4 text-xs text-muted-foreground">
									<span>
										<Trans>MAC Address</Trans>
									</span>
									<span className="font-mono">{info.mac_address || "N/A"}</span>
								</div>
								<div className="pt-2 pl-4">
									<Button
										size="sm"
										variant="outline"
										onClick={triggerManualWake}
										disabled={isWaking}
									>
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
									<span>{info.esp_name || "ESP32 Module"}</span>
								</div>
								<div className="flex justify-between pl-4 text-xs text-muted-foreground">
									<span>
										<Trans>ESP IP Address</Trans>
									</span>
									<span>{info.esp_ip || "N/A"}</span>
								</div>
								<div className="flex justify-between pl-4 text-xs text-muted-foreground">
									<span>
										<Trans>Relay Channel</Trans>
									</span>
									<span className="font-mono">{info.esp_channel || "N/A"}</span>
								</div>
								<div className="pt-2 pl-4">
									<Button
										size="sm"
										variant="outline"
										onClick={triggerManualRelay}
										disabled={isTriggering}
									>
										{isTriggering ? <LoaderCircle className="h-3 w-3 animate-spin mr-1.5" /> : null}
										<Trans>Trigger Relay Press</Trans>
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
				<CardContent className="h-[220px] overflow-y-auto">
					{loading ? (
						<div className="flex items-center justify-center h-full text-muted-foreground text-sm">
							<Trans>Loading events...</Trans>
						</div>
					) : events.length === 0 ? (
						<div className="flex items-center justify-center h-full text-muted-foreground text-sm">
							<Trans>No recent recovery events</Trans>
						</div>
					) : (
						<div className="relative pl-6 border-l space-y-4 max-h-[300px] overflow-y-auto mt-2">
							{events.map((ev, idx) => {
								let dotColor = "bg-muted-foreground"
								let textColor = "text-foreground"
								if (ev.event.includes("SUCCESS") || ev.event.includes("RECOVERED")) {
									dotColor = "bg-green-500"
									textColor = "text-green-600 dark:text-green-400"
								} else if (ev.event.includes("FAILED") || ev.event.includes("FAILURE") || ev.event.includes("ERROR")) {
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
										<span className={`absolute -left-[30px] top-1.5 flex h-2 w-2 rounded-full ring-4 ring-background ${dotColor}`} />
										<div className="flex flex-col">
											<span className={`text-xs font-semibold ${textColor}`}>{ev.event}</span>
											<span className="text-[10px] text-muted-foreground">
												{new Date(ev.timestamp).toLocaleString()}
											</span>
											{ev.metadata && (
												<pre className="text-[10px] bg-muted/40 p-1.5 rounded mt-1 overflow-x-auto max-w-full font-mono text-muted-foreground leading-normal">
													{typeof ev.metadata === "string"
														? ev.metadata
														: JSON.stringify(ev.metadata, null, 2)}
												</pre>
											)}
										</div>
									</div>
								)
							})}
						</div>
					)}
				</CardContent>
			</Card>
		</div>
	)
}
