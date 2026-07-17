import { Component, type ErrorInfo, type ReactNode } from "react"

interface ErrorBoundaryProps {
	children: ReactNode
	/** Optional fallback UI. Defaults to rendering nothing. */
	fallback?: ReactNode
}

interface ErrorBoundaryState {
	hasError: boolean
}

/**
 * Catches render errors in child components so they don't unmount the
 * entire parent tree. Used to isolate optional UI sections (e.g. the
 * Recovery Nodes panel) from the core dashboard.
 */
export class ErrorBoundary extends Component<ErrorBoundaryProps, ErrorBoundaryState> {
	constructor(props: ErrorBoundaryProps) {
		super(props)
		this.state = { hasError: false }
	}

	static getDerivedStateFromError(): ErrorBoundaryState {
		return { hasError: true }
	}

	componentDidCatch(error: Error, info: ErrorInfo) {
		console.error("ErrorBoundary caught:", error, info.componentStack)
	}

	render() {
		if (this.state.hasError) {
			return this.props.fallback ?? null
		}
		return this.props.children
	}
}
