import { Trans } from "@lingui/react/macro"
import { useStore } from "@nanostores/react"
import { getPagePath } from "@nanostores/router"
import { ShieldCheckIcon } from "lucide-react"
import { useEffect, useState } from "react"
import { isModuleOnline } from "@/lib/recoveryManager"
import { $recoveryChannels, $recoveryModules } from "@/lib/stores"
import { cn, timeAgo } from "@/lib/utils"
import { $router, Link } from "./router"
import { Badge } from "./ui/badge"
import { Card, CardContent, CardHeader, CardTitle } from "./ui/card"

/** Recovery Nodes panel: one tile per ESP32 hardware recovery module, shown
 * on the home dashboard so their status is visible without opening Settings.
 * Renders nothing when no modules are registered. */
export function RecoveryNodes() {
	const modules = useStore($recoveryModules)
	const channels = useStore($recoveryChannels)

	// Liveness is derived from last_ping + ping_interval_seconds on every
	// render, but nothing changes those props when a module simply goes
	// quiet - a realtime event only fires on a record write. This tick
	// forces a periodic re-render so a silently-dead module still flips to
	// OFFLINE without waiting for its next ping or an unrelated edit.
	const [, setTick] = useState(0)
	useEffect(() => {
		const id = setInterval(() => setTick((t) => t + 1), 30_000)
		return () => clearInterval(id)
	}, [])

	const moduleList = Object.values(modules).filter((mod) => mod.status !== "unapproved" && mod.status !== "rejected")
	if (moduleList.length === 0) {
		return null
	}

	const channelList = Object.values(channels)

	return (
		<Card>
			<CardHeader className="pb-4 px-2 sm:px-6 max-sm:pt-5 max-sm:pb-1">
				<div className="px-2 sm:px-1">
					<CardTitle>
						<Trans>Recovery Nodes</Trans>
					</CardTitle>
				</div>
			</CardHeader>
			<CardContent className="max-sm:p-2">
				<div className="grid sm:grid-cols-2 lg:grid-cols-3 2xl:grid-cols-4 gap-3">
					{moduleList.map((mod) => {
						const online = isModuleOnline(mod)
						const isDisabled = mod.status === "disabled"
						const moduleChannels = channelList.filter((ch) => ch.module === mod.id)
						const protectedCount = moduleChannels.filter((ch) => ch.system && !ch.hardware_recovery_disabled).length

						return (
							<div
								key={mod.id}
								className="relative border border-foreground/10 rounded-lg p-3.5 hover:-translate-y-px duration-200 hover:shadow-md shadow-black/5 bg-transparent"
							>
								<div className="flex items-center justify-between gap-2 mb-2.5">
									<div className="flex items-center gap-1.5 min-w-0">
										<ShieldCheckIcon
											className={cn("size-4 shrink-0", online ? "text-green-500" : "text-muted-foreground")}
										/>
										<span className="font-medium truncate">{mod.name}</span>
									</div>
									<Badge variant={isDisabled ? "secondary" : online ? "success" : "destructive"}>
										{isDisabled ? <Trans>DISABLED</Trans> : online ? <Trans>ONLINE</Trans> : <Trans>OFFLINE</Trans>}
									</Badge>
								</div>
								<div className="grid grid-cols-2 gap-y-1.5 text-xs text-muted-foreground">
									<span>
										<Trans>Channels</Trans>
									</span>
									<span className="text-foreground text-right">
										{protectedCount} / {mod.max_channels}
									</span>
									{mod.temperature !== undefined && mod.temperature > 0 && (
										<>
											<span>
												<Trans>Temp</Trans>
											</span>
											<span className="text-foreground text-right">{mod.temperature.toFixed(1)}°C</span>
										</>
									)}
									<span>
										<Trans>Last Heartbeat</Trans>
									</span>
									<span className="text-foreground text-right">{timeAgo(mod.last_ping)}</span>
								</div>
								<Link
									href={getPagePath($router, "settings", { name: "recovery-modules" })}
									className="absolute inset-0 w-full h-full"
									aria-label={mod.name}
								></Link>
							</div>
						)
					})}
				</div>
			</CardContent>
		</Card>
	)
}
