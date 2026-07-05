import os, glob

for fn in glob.glob("*.cpp") + glob.glob("*.h"):
    with open(fn, 'r') as f:
        content = f.read()

    orig_content = content

    content = content.replace('#include <list>', '#include <vector>')
    content = content.replace('std::list < particle >', 'std::vector < particle >')
    content = content.replace('std::list<particle>', 'std::vector<particle>')

    # addParticle implementation
    content = content.replace('particles.push_front (particle ());\n\treturn &(*particles.begin ());', 'particles.push_back (particle ());\n\treturn &particles.back();')

    # Erase loop in skyrocket.cpp
    content = content.replace('curpart = particles.erase(curpart)--;', 'if (particles.size() > 1) { particles[i] = particles.back(); } particles.pop_back(); curpart = &particles[i]; continue;')

    # Replace iterators with size_t loops
    def fix_loops(text):
        lines = text.split('\n')
        out = []
        in_loop = False
        loop_var = ""
        for i, line in enumerate(lines):
            if 'std::vector < particle >::iterator' in line and 'particles.begin ()' in line:
                loop_var = line.split('iterator')[1].split('=')[0].strip()
                out.append('\t\tfor (size_t i = 0; i < particles.size(); i++) {')
                out.append(f'\t\t\tparticle *{loop_var} = &particles[i];')
                in_loop = True
                continue
            if in_loop and f'while ({loop_var} != particles.end ())' in line:
                continue # Skip the while line since we use for loop
            if in_loop and f'{loop_var}++;' in line:
                continue # Skip increment
            
            # For pointer deref instead of iterator deref, actually curpart-> works for both pointers and iterators!
            out.append(line)
        return '\n'.join(out)
    
    # Actually it is easier to write specific regexes.
    pass
