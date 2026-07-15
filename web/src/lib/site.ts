/** Site / repo constants for GitHub integration */
export const SITE = {
  owner: 'Vu2n',
  repo: 'icky',
  get repoUrl() {
    return `https://github.com/${this.owner}/${this.repo}`
  },
  get pagesUrl() {
    return `https://${this.owner.toLowerCase()}.github.io/${this.repo}/`
  },
  get newDumpIssueUrl() {
    return `${this.repoUrl}/issues/new?template=dump_submission.yml`
  },
}

export function buildDumpIssueUrl(opts: {
  game: string
  engine: string
  slug: string
  types: number
  globals: number
  mode: string
}): string {
  const title = `Dump: ${opts.game}`
  // template + labels — user still needs to attach the file
  const params = new URLSearchParams({
    template: 'dump_submission.yml',
    title,
    labels: 'dump-submission',
  })
  return `${SITE.repoUrl}/issues/new?${params.toString()}`
}
