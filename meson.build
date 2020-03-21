project('mpdfm', 'cpp', default_options : ['cpp_std=c++17'])

libmpdclient = dependency('libmpdclient')
openssl = dependency('openssl')
boost = dependency('boost', modules : ['filesystem', 'system'])
threads = dependency('threads')
spdlog = dependency('spdlog', fallback: ['spdlog', 'spdlog_dep'])

incl = include_directories('include')
pegtl = include_directories('subprojects/PEGTL/include')
taojson = include_directories('subprojects/json/include')
gsl = include_directories('subprojects/GSL/include')

src = [
    'src/main.cpp', 'src/mpc.cpp', 'src/scrobbler.cpp',
    'src/protocols/as20.cpp', 'src/http_client.cpp', 'src/uris.cpp',
    'src/config/config_file.cpp', 'src/directory_helper.cpp'
]

add_project_arguments('-DGSL_THROW_ON_CONTRACT_VIOLATION', language : 'cpp')
executable('mpdfm', src,
           dependencies : [libmpdclient, threads, openssl, spdlog, boost],
           include_directories : [incl, pegtl, taojson, gsl],
           override_options : ['cpp_std=c++17'],
           install : true)
