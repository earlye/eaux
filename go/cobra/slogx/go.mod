module github.com/earlye/eaux/go/cobra/slogx

go 1.25.3

replace (
	github.com/earlye/eaux/go/env => ../../env
	github.com/earlye/eaux/go/log => ../../log
)

require (
	github.com/earlye/eaux/go/env v0.0.0-00010101000000-000000000000
	github.com/earlye/eaux/go/log v0.0.0-00010101000000-000000000000
	github.com/spf13/cobra v1.8.1
)

require (
	github.com/inconshreveable/mousetrap v1.1.0 // indirect
	github.com/spf13/pflag v1.0.5 // indirect
)
