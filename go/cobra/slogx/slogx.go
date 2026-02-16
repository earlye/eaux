// Package slogx provides Cobra integration for github.com/earlye/eaux/go/log:
// persistent flags (verbosity, log-format) and a PreRunE that configures slog from those flags.
// Import this package only from apps that already use Cobra; the main logging package has no Cobra dependency.
package slogx

import (
	"log/slog"

	env "github.com/earlye/eaux/go/env"
	logging "github.com/earlye/eaux/go/log"
	"github.com/spf13/cobra"
)

// Options configures AddPersistentFlags (env vars and defaults for verbosity and log-format).
type Options struct {
	VerbosityEnv     string // e.g. "APPLICATION_VERBOSITY"
	VerbosityDefault string // e.g. "INFO"
	LogFormatEnv     string // e.g. "APPLICATION_LOG_FORMAT"
	LogFormatDefault string // e.g. "text"
}

// AddPersistentFlags adds --verbosity/-v and --log-format/-f to cmd.
// Flag default values are taken from the env vars in opts when set, otherwise the Default fields.
func AddPersistentFlags(cmd *cobra.Command, opts Options) {
	verbosityDefault := env.GetenvDefault(opts.VerbosityEnv, opts.VerbosityDefault)
	logFormatDefault := env.GetenvDefault(opts.LogFormatEnv, opts.LogFormatDefault)

	cmd.PersistentFlags().StringP("verbosity", "v", verbosityDefault,
		"Display log messages with selected or greater level.\n"+
			"Valid choices: SILLY, TRACE, DEBUG, INFO, WARN, ERROR.\n"+
			"If specified without a value, uses DEBUG.")
	cmd.PersistentFlags().Lookup("verbosity").NoOptDefVal = "DEBUG"

	cmd.PersistentFlags().StringP("log-format", "f", logFormatDefault,
		"Set the log format (text, json).")
}

// SlogPreRunE is a cobra.PersistentPreRunE that sets slog.Default from cmd's verbosity and log-format flags.
// Use it as rootCmd.PersistentPreRunE = slogx.SlogPreRunE.
// The cmd passed at run time (e.g. root or subcommand) is used to read the flags.
func SlogPreRunE(cmd *cobra.Command, args []string) error {
	level, err := logging.SlogLevel(cmd.Flag("verbosity").Value.String())
	if err != nil {
		return err
	}
	opts := &slog.HandlerOptions{Level: level}
	h := logging.SlogHandler(cmd.Flag("log-format").Value.String(), opts)
	slog.SetDefault(slog.New(h))
	return nil
}
