import { t } from "@lingui/core/macro"
import { Trans } from "@lingui/react/macro"
import { redirectPage } from "@nanostores/router"
import { LoaderCircleIcon, SendIcon, SaveIcon } from "lucide-react"
import { useEffect, useState } from "react"
import { $router } from "@/components/router"
import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import { Separator } from "@/components/ui/separator"
import { toast } from "@/components/ui/use-toast"
import { Input } from "@/components/ui/input"
import { Label } from "@/components/ui/label"
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from "@/components/ui/select"
import { isAdmin, pb } from "@/lib/api"

interface HeartbeatStatus {
	enabled: boolean
	url?: string
	interval?: number
	method?: string
	msg?: string
}

export default function HeartbeatSettings() {
	const [status, setStatus] = useState<HeartbeatStatus | null>(null)
	const [isLoading, setIsLoading] = useState(true)
	const [isSaving, setIsSaving] = useState(false)
	const [isTesting, setIsTesting] = useState(false)
	const [url, setUrl] = useState("")
	const [interval, setInterval] = useState("60")
	const [method, setMethod] = useState("POST")

	if (!isAdmin()) {
		redirectPage($router, "settings", { name: "general" })
	}

	useEffect(() => {
		fetchStatus()
	}, [])

	async function fetchStatus() {
		try {
			setIsLoading(true)
			const res = await pb.send<HeartbeatStatus>("/api/beszel/heartbeat-status", {})
			setStatus(res)
			setUrl(res.url ?? "")
			setInterval(String(res.interval ?? 60))
			setMethod(res.method ?? "POST")
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

	async function handleSubmit(e: React.FormEvent<HTMLFormElement>) {
		e.preventDefault()
		setIsSaving(true)
		try {
			await pb.send("/api/beszel/heartbeat-status", {
				method: "POST",
				body: {
					url,
					interval: Number(interval),
					method,
				},
			})
			toast({
				title: t`Settings saved successfully`,
				description: t`Outbound heartbeat has been updated.`,
			})
			await fetchStatus()
		} catch (error: unknown) {
			toast({
				title: t`Error`,
				description: (error as Error).message,
				variant: "destructive",
			})
		} finally {
			setIsSaving(false)
		}
	}

	async function sendTestHeartbeat() {
		setIsTesting(true)
		try {
			const res = await pb.send<{ err: string | false }>("/api/beszel/test-heartbeat", {
				method: "POST",
			})
			if ("err" in res && !res.err) {
				toast({
					title: t`Heartbeat sent successfully`,
					description: t`Check your monitoring service`,
				})
			} else {
				toast({
					title: t`Error`,
					description: (res.err as string) ?? t`Failed to send heartbeat`,
					variant: "destructive",
				})
			}
		} catch (error: unknown) {
			toast({
				title: t`Error`,
				description: (error as Error).message,
				variant: "destructive",
			})
		} finally {
			setIsTesting(false)
		}
	}

	return (
		<div>
			<div>
				<h3 className="text-xl font-medium mb-2">
					<Trans>Heartbeat Monitoring</Trans>
				</h3>
				<p className="text-sm text-muted-foreground leading-relaxed">
					<Trans>
						Send periodic outbound pings to an external monitoring service so you can monitor Beszel without exposing it
						to the internet.
					</Trans>
				</p>
			</div>
			<Separator className="my-4" />

			{isLoading ? (
				<div className="animate-pulse space-y-4">
					<div className="h-8 bg-muted rounded w-1/3"></div>
					<div className="h-32 bg-muted rounded"></div>
				</div>
			) : (
				<form onSubmit={handleSubmit} className="space-y-5">
					<div className="grid gap-4">
						<div className="grid gap-2">
							<Label htmlFor="url">
								<Trans>Endpoint URL</Trans>
							</Label>
							<Input
								id="url"
								type="text"
								placeholder="https://uptime.betterstack.com/api/v1/heartbeat/xxxx"
								value={url}
								onChange={(e) => setUrl(e.target.value)}
								className="font-mono"
							/>
							<p className="text-xs text-muted-foreground">
								<Trans>Endpoint URL to ping. Leave blank to disable heartbeat monitoring.</Trans>
							</p>
						</div>

						<div className="grid sm:grid-cols-2 gap-4">
							<div className="grid gap-2">
								<Label htmlFor="interval">
									<Trans>Interval (seconds)</Trans>
								</Label>
								<Input
									id="interval"
									type="number"
									min={1}
									placeholder="60"
									value={interval}
									onChange={(e) => setInterval(e.target.value)}
								/>
								<p className="text-xs text-muted-foreground">
									<Trans>Seconds between pings.</Trans>
								</p>
							</div>

							<div className="grid gap-2">
								<Label htmlFor="method">
									<Trans>HTTP Method</Trans>
								</Label>
								<Select value={method} onValueChange={setMethod}>
									<SelectTrigger id="method">
										<SelectValue />
									</SelectTrigger>
									<SelectContent>
										<SelectItem value="POST">POST</SelectItem>
										<SelectItem value="GET">GET</SelectItem>
										<SelectItem value="HEAD">HEAD</SelectItem>
									</SelectContent>
								</Select>
								<p className="text-xs text-muted-foreground">
									<Trans>HTTP method used for pinging.</Trans>
								</p>
							</div>
						</div>
					</div>

					<Button type="submit" className="flex items-center gap-1.5" disabled={isSaving}>
						{isSaving ? (
							<LoaderCircleIcon className="h-4 w-4 animate-spin" />
						) : (
							<SaveIcon className="h-4 w-4" />
						)}
						<Trans>Save Settings</Trans>
					</Button>

					{status?.enabled && (
						<>
							<Separator className="my-4" />
							<div className="space-y-5">
								<div className="flex items-center gap-2">
									<Badge variant="success">
										<Trans>Active</Trans>
									</Badge>
								</div>
								<div>
									<h4 className="text-base font-medium mb-1">
										<Trans>Test heartbeat</Trans>
									</h4>
									<p className="text-sm text-muted-foreground leading-relaxed mb-3">
										<Trans>Send a single heartbeat ping to verify your endpoint is working.</Trans>
									</p>
									<Button
										type="button"
										variant="outline"
										className="flex items-center gap-1.5"
										onClick={sendTestHeartbeat}
										disabled={isTesting}
									>
										{isTesting ? (
											<LoaderCircleIcon className="size-4 animate-spin" />
										) : (
											<SendIcon className="size-4" />
										)}
										<Trans>Send test heartbeat</Trans>
									</Button>
								</div>

								{method === "POST" && (
									<>
										<Separator />
										<div>
											<h4 className="text-base font-medium mb-2">
												<Trans>Payload format</Trans>
											</h4>
											<p className="text-sm text-muted-foreground leading-relaxed mb-2">
												<Trans>
													When using POST, each heartbeat includes a JSON payload with system status summary, list of down systems,
													and triggered alerts.
												</Trans>
											</p>
											<p className="text-sm text-muted-foreground leading-relaxed">
												<Trans>
													The overall status is <code className="bg-muted rounded-sm px-1 text-primary">ok</code> when all systems are
													up, <code className="bg-muted rounded-sm px-1 text-primary">warn</code> when alerts are triggered, and{" "}
													<code className="bg-muted rounded-sm px-1 text-primary">error</code> when any system is down.
												</Trans>
											</p>
										</div>
									</>
								)}
							</div>
						</>
					)}
				</form>
			)}
		</div>
	)
}
