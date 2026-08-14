require 'mkmf'

root = File.expand_path('../..', __dir__)

$INCFLAGS << " -I#{File.join(root, 'include')}"
$VPATH << File.join(root, 'src')
$srcs = [
  'carbon_ruby.c',
  'carbon.c'
]

create_makefile('carbon')
