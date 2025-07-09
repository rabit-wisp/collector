'use strict';
'require form';
'require uci';
'require network';
'require tools.widgets as widgets';
'require view';

return view.extend({
	render: function() {
		let m, s, o;

    m = new form.Map('statsd', _('Wireless Stats Clients'),
    _('Configure wireless-stats collection endpoints.'));

    s = m.section(form.TypedSection, 'statsd', _('Instance'));
    s.anonymous = true;
    s.addremove = true;

    o = s.option(widgets.NetworkSelect, 'interface', _('Interface'), _('Interface on which to listen.'));
    o.optional = false;
    o.nocreate = false;
    o.rmempty = false;
    o.depends({ enabled: '1' });


    o = s.option(widgets.DeviceSelect, '_net_device', _('Device'));
    o.ucioption = 'device';
    o.nobridges = false;
    o.optional = false;
    o.filter = function(section_id, value) {
        // Filter out aliases (starting with @)
        if (value && value.charAt(0) === '@')
            return false;
        return true;
    }
    o.value('', _('-- Please choose --'));


    o = s.option(form.ListValue, 'output_type', _('Output Type'));
    o.value('udp', 'UDP');
    o.value('tcp', 'TCP');
    o.value('zmq', 'ZMQ');
    o.value('stdout', 'stdout (for developers)');
    o.value('stderr', 'stderr (for developers)');
    o.default = 'udp';

    o = s.option(form.Value, 'dest_ip', _('Destination IP'));
    o.datatype = 'ip4addr';
    o.depends('output_type', 'udp');
    o.depends('output_type', 'tcp');

    o = s.option(form.Value, 'dest_port', _('Destination Port'));
    o.datatype = 'port';
    o.depends('output_type', 'udp');
    o.depends('output_type', 'tcp');

    o = s.option(form.DynamicList, 'endpoint', _('ZMQ Endpoint(s)'));
    o.placeholder = 'tcp://*:8000';
    o.depends('output_type', 'zmq');

    o = s.option(form.ListValue, 'mode', _('ZMQ Mode'));
    o.value('connect', 'Connect');
    o.value('bind', 'Bind');
    o.default = 'connect';
    o.depends('output_type', 'zmq');

    o = s.option(form.Value, 'interval', _('Interval (ms)'));
    o.datatype = 'uinteger';
    o.placeholder = '1000';

    o = s.option(form.Flag, 'no_compress', _('Disable Compression'));

    o = s.option(form.Value, 'ping_frequency', _('Ping Frequency (ms)'));
    o.datatype = 'uinteger';
    o.placeholder = '1000';

    o = s.option(form.DynamicList, 'ping_hosts', _('Ping Hosts'), _('Hosts to monitor for ICMP ping latency'));
    o.datatype = 'host';


    return m.render();
	}
});
