import { useLingui } from "@lingui/react/macro"
import { memo, Suspense, useEffect, useMemo } from "react"
import SystemsTable from "@/components/systems-table/systems-table"
import { ActiveAlerts } from "@/components/active-alerts"
import { ErrorBoundary } from "@/components/error-boundary"
import { FooterRepoLink } from "@/components/footer-repo-link"
import { RecoveryNodes } from "@/components/recovery-nodes"

export default memo(() => {
	const { t } = useLingui()

	useEffect(() => {
		document.title = `${t`All Systems`} / Beszel X Harix`
	}, [t])

	return useMemo(
		() => (
			<>
				<div className="flex flex-col gap-4">
					<ActiveAlerts />
					<ErrorBoundary>
						<RecoveryNodes />
					</ErrorBoundary>
					<Suspense>
						<SystemsTable />
					</Suspense>
				</div>
				<FooterRepoLink />
			</>
		),
		[]
	)
})
