mandir = join_paths(get_option('prefix'), get_option('mandir'))
man3dir = join_paths(mandir, 'man3')

conf = configuration_data()
conf.set('version', meson.project_version())
conf.set('version_date', version_date)

manpage_lxi_connect = configure_file(
     input: files('lxi_connect.3.in'),
     output: 'lxi_connect.3',
     configuration: conf,
)

manpage_lxi_disconnect = configure_file(
     input: files('lxi_disconnect.3.in'),
     output: 'lxi_disconnect.3',
     configuration: conf,
)

manpage_lxi_discover = configure_file(
     input: files('lxi_discover.3.in'),
     output: 'lxi_discover.3',
     configuration: conf,
)
manpage_lxi_init = configure_file(
     input: files('lxi_init.3.in'),
     output: 'lxi_init.3',
     configuration: conf,
)
manpage_lxi_receive = configure_file(
     input: files('lxi_receive.3.in'),
     output: 'lxi_receive.3',
     configuration: conf,
)
manpage_lxi_send = configure_file(
     input: files('lxi_send.3.in'),
     output: 'lxi_send.3',
     configuration: conf,
)

manpages = [
            manpage_lxi_connect,
            manpage_lxi_disconnect,
            manpage_lxi_init,
            manpage_lxi_discover,
            manpage_lxi_receive,
            manpage_lxi_send,
            ]

install_man(
     manpages,
     install_dir: man3dir,
)
