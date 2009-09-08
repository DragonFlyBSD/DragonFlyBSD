#!/usr/bin/env ruby

require 'find'
require 'open3'

verbose = false

pmoddeps = File.join(File.dirname(__FILE__), 'pmoddeps.gdb')

provides = {}
depends = {}

Find.find(*ARGV) do |path|
  next if not FileTest.file?(path)
  next if not (path.end_with?('.ko') or ['kernel', 'kernel.debug'].include?(File.basename(path)))

  mv = {}
  md = {}

  Open3.popen3(pmoddeps + ' ' + path) do |pin, pout, perr|
    pin.close
    pout.each do |line|
      f = line.split
      case f[0]
      when 'module'
        # ignore?
      when 'version'
        mv[f[1]] = f[2].to_i
      when 'depend'
        md[f[1]] = f[2..-1].map{|e| e.to_i}
      end
    end
  end

  modname = File.basename(path)

  provides[modname] = mv
  depends[modname] = md
end

kernel = provides.select{|pmn, pd| pmn.start_with?('kernel')}

depends.each do |modname, md|
  md.each do |depname, vers|
    minv, pref, maxv = vers

    chk = proc do |h|
      not h.select{|pn, pv| pn == depname and pv >= minv and pv <= maxv}.empty?
    end

    defm = []
    # try module itself
    defm << [modname, provides[modname]]
    # try kernel
    defm += kernel
    # try module depend name
    defm << [depname + '.ko', provides[depname + '.ko']]

    defm.reject!{|e, v| not v}
    match = defm.select{|mn, k| chk.call(k)}
    if not match.empty?
      puts "#{modname} depend #{depname} found in #{match[0][0]}" if verbose
      next
    end

    # else not found in the right place

    match = provides.select do |pmn, pd|
      chk.call(pd)
    end

    if match.empty?
      $stderr.puts "#{modname} depend #{depname} #{minv} #{pref} #{maxv} not found"
    else
      match.each do |m|
        $stderr.puts "#{modname} depend #{depname} found in #{m[0]} instead"
      end
    end
  end
end
