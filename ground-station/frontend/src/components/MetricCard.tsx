import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card"

type MetricVariant = "default" | "good" | "warning" | "bad"

type MetricCardProps = {
  title: string
  value: string
  subtitle?: string
  variant?: MetricVariant
}

const borderClass: Record<MetricVariant, string> = {
  default: "border-l-slate-400",
  good: "border-l-green-600",
  warning: "border-l-amber-500",
  bad: "border-l-red-600",
}

export function MetricCard({
  title,
  value,
  subtitle,
  variant = "default",
}: MetricCardProps) {
  return (
    <Card className={`border-l-4 ${borderClass[variant]}`}>
      <CardHeader className="pb-2">
        <CardTitle className="text-sm font-medium text-muted-foreground">
          {title}
        </CardTitle>
      </CardHeader>
      <CardContent>
        <div className="text-2xl font-bold">{value}</div>
        {subtitle ? (
          <p className="mt-1 text-xs text-muted-foreground">{subtitle}</p>
        ) : null}
      </CardContent>
    </Card>
  )
}