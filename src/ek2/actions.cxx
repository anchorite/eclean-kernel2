/* eclean-kernel2
 * (c) 2017 Michał Górny
 * 2-clause BSD license
 */

#ifdef HAVE_CONFIG_H
#	include "config.h"
#endif

#include "ek2/actions.h"
#include "ek2/judge.h"
#include "ek2/judges.h"
#include "ek2/options.h"

#include "ek2/util/error.h"

#include <cassert>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

void list_kernels(const Layout& l)
{
	for (const FileSet& fs : l.kernels())
	{
		std::cerr << fs.pretty_version() << ":\n";
		for (const std::shared_ptr<File>& f : fs.files())
			std::cerr << "- " << f->type() << ": " << f->path() << "\n";
	}
}

void remove(Layout& l, const Options& opts,
		std::vector<std::unique_ptr<BootLoader>>& bootloaders)
{
	std::vector<std::unique_ptr<Judge>> judges = get_judges(opts);
	fileset_vote_map fileset_votes;
	file_vote_map file_votes;

	// pre-fill the map with kernels, in order
	for (FileSet& fs : l.kernels())
		fileset_votes.emplace_back(&fs);
	assert(fileset_votes.size() == l.kernels().size());

	for (const std::unique_ptr<Judge>& j : judges)
		j->judge(fileset_votes, file_votes);

	// check which kernels to remove, which to keep
	// count the former, collect files needed for the latter
	size_t to_remove = 0;

	for (const FileSetVoteSet& fsv : fileset_votes)
	{
		if (!fsv.votes.empty() && fsv.votes.back().remove)
			++to_remove;
	}

	bool pretend = opts.pretend;

	if (to_remove == 0)
	{
		std::cerr << "No kernels to remove\n";
		return;
	}

	// failsafe: do not remove all kernels!
	if (!pretend && to_remove == l.kernels().size())
	{
		std::cerr << "All kernels would be removed! This is most likely wrong. Will run\n"
			"in --pretend mode to avoid breaking your system.\n\n";
		pretend = true;
	}

	if (pretend)
		std::cerr << "The following kernels would be removed:\n";
	else
		std::cerr << "Removing...\n";

	for (FileSetVoteSet& fsv : fileset_votes)
	{
		// keeping this kernel
		// TODO: support file removal for non-removed kernels?
		if (fsv.votes.empty() || !fsv.votes.back().remove)
			continue;

		std::cerr << "\n== " << fsv.key->pretty_version() << " ==\n"
			"Rationale:\n";
		for (const Vote& v : fsv.votes)
		{
			std::cerr << (v.remove ? "[-] " : "[+] ")
				<< v.reason << "\n";
		}

		std::cerr << "Files:\n";
		for (std::shared_ptr<File>& f : fsv.key->files())
		{
			file_vote_map::iterator file_vote;
			try
			{
				file_vote = file_votes.find(f->id());
			}
			catch (const IOError& e)
			{
				if (e.err() == ENOENT)
				{
					std::cerr << "! " << f->path()
						<< " (does not exist)" << std::endl;
					continue;
				}
				else
					throw;
			}

			// file used by kept kernels
			if (file_vote != file_votes.end()
					&& !file_vote->second.empty()
					&& !file_vote->second.back().remove)
			{
				// TODO: support multiple reasons
				std::cerr << "+ " << f->path()
					<< " (" << file_vote->second.back().reason << ')'
					<< std::endl;
			}
			else
			{
				std::cerr << "- " << f->path() << std::endl;
				if (!pretend)
					f->remove();
			}
		}
	}

	for (std::unique_ptr<BootLoader>& b : bootloaders)
		b->postrm();
}
